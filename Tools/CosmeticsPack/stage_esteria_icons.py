"""Stage matched icons for loot crates/keys, Fortnite weapons and Fortnite emotes, then install them.

  python stage_esteria_icons.py stage --out G:/FortniteCosmetics/esteria-icons-<date>
  python stage_esteria_icons.py install G:/FortniteCosmetics/esteria-icons-<date>    (WoW closed)

Staging never touches the client. Weapon icons follow the backpack rule: exact normalized mesh/package/cosmetic
names, plus explicit aliases checked by eye; no fuzzy scoring, and nothing is substituted across cosmetics.
Unmatched items keep their current icon and are listed in receipt.json with the reason.

The registry clone changes only ItemDisplayInfo field 5 (inventory icon) of matched weapon displays and appends one
ItemDisplayInfo and one Item row per crate/key; every other row is asserted unchanged. Icons go in their own
archive that sorts after every other patch; the edited addon Lua goes back into the archives that already own it.
"""
import argparse
import hashlib
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "Tools" / "RetailAssets"))
sys.path.insert(0, r"R:\Users\Zach\Documents\GitHub\EsteriaWoW\tools")
import native_import as native  # noqa: E402
import cars_mount_pack  # noqa: E402
from backpack_icons import blp_bytes  # noqa: E402

CLIENT = Path("G:/3.3.5a - Dev")
STORM_DLL = REPO / "Tools" / "MountStudio" / "runtime" / "backend" / "StormLib.dll"
# The client sorts patch archives case-insensitively, highest name first: 30 Z's outrank every installed archive.
ARCHIVE = "Patch-" + "Z" * 30 + "EsteriaIcons.MPQ"
ICON_SIZE = 128
B = "\\"

ESTERIA_ICONS = Path(r"R:\Users\Zach\Documents\GitHub\EsteriaWoW\NewModels\_Interface\_Icons")
FORTNITE_ICONS = Path("G:/FortnitePorter/Exported/Icons")
WEAPON_CATALOGS = [Path("G:/FortniteCosmetics/collection-20261114/weapons/catalog.json"),
                   Path("G:/FortniteCosmetics/collection-revision-20261114/catalog.json")]
EMOTE_REGISTRY = Path(r"R:\Users\Zach\Documents\GitHub\WoW-Dance-Studio\Output\Esteria\EmoteRegistry.json")
# Official pet carrier icons from fortnite-api.com (fortnite-api-petcarriers.json beside them), by pet cloak display.
PET_ICONS = Path("G:/FortniteCosmetics/pet-icons-20261116")
PETS = {3400100: "PetCarrier_005_Wolf", 3400101: "PetCarrier_001_Dog", 3400102: "PetCarrier_009_Cat",
        3400103: "PetCarrier_003_Dragon", 3400104: "PetCarrier_012_Drift_Fox", 3400105: "PetCarrier_008_Fox",
        3400106: "PetCarrier_004_Hamster", 3400107: "PetCarrier_010_RoboKitty (small icon only)",
        3400108: "PetCarrier_007_Woodsy"}
# The elemental swords are not Fortnite cosmetics: their icons are the Blender previews of their own models.
SWORD_RENDERS = Path("G:/FortniteCosmetics/elemental-swords-20261114")
SWORDS = (3430084, 3430085, 3430086)

# item entry (= new display id) -> source art. The registry is an overlay holding only Esteria's rows, so the new
# display rows are icon-only: every other field 0 (no model, no textures, empty strings).
CRATES = {
    3450101: ESTERIA_ICONS / "Crates" / "Epic Purple-Gold Enchanted Treasure Chest-9.png",
    3450001: ESTERIA_ICONS / "Keys" / "Epic Violet Crate Key-20.png",
}
# Item.dbc: id, class, subclass, sound override, material, display, inventory type, sheath (miscellaneous, unequippable)
ITEM_ROW = [0, 15, 4, 0xFFFFFFFF, 1, 0, 0, 0]

# Weapon mesh stem -> icon, where names differ; each was compared by eye with the mesh's own diffuse texture.
WEAPON_ALIASES = {
    "SK_PickAxe_Jaws": "T-Icon-Pickaxes-SharkAttackPickaxe.png",
    "Cyber_Armor_Female_Axe": "T-Icon-Pickaxes-CyberArmor2HPickaxe.png",
    "Desert_Shadow_Male_Axe_L": "T-Icon-Pickaxes-DesertShadowPickaxe.png",
    "Desert_Shadow_Male_Axe_R": "T-Icon-Pickaxes-DesertShadowPickaxe.png",
    "Pickaxe_EvilCowboy": "T-Icon-Pickaxes-Pickaxe-ID-115-EvilCowboy.png",
    "Firecracker_Male_Axe": "T-Icon-Pickaxes-FirecrackerPickaxe.png",
    "GarlicWhisk_Axe": "T_Icon_Pickaxes_GarlicWhisk_L.png",
    "Pickaxe_GuanYu": "T-Icon-Pickaxes-Pickaxe-ID-112-GuanYu.png",
    "Jonesy_Cattle_Male_Axe": "T-Icon-Pickaxes-JonesyCattlePickaxe.png",
    "Journey_Mentor_Female_Axe": "T-Icon-Pickaxes-JourneyMentor-DefaultPickaxes.png",
    "Knight_Cat_Female_Axe": "T-Icon-Pickaxes-KnightCatPickaxes-default.png",
    "Lancelot_Male_Axe": "T-Icon-Pickaxes-LancelotPickaxe.png",
    "Lollipop_Trickster_Female": "T-Icon-Pickaxes-LollipopTricksterPickaxe.png",
    "Mystic_Male": "T-Icon-Pickaxes-MysticPickaxe.png",
    "Parfait_Female_Axe": "T-Icon-Pickaxes-ParfaitPickaxe.png",
    "SK_Pickaxe_PumpkinSlice_DEMO": "T-Icon-Pickaxes-Pickaxe-ID-108-PumpkinSlice.png",
    "SK_Pickaxe_RedRiding": "T-Icon-Pickaxes-Pickaxe-ID-102-RedRiding.png",
    # The only Cold-Hot Hitters icon shows the fire and ice pair together; it fits both variants.
    "SM_Pickaxe_SteamPowerOrange_None": "T-Icon-Pickaxes-SteamPower-Blue.png",
    "Street_Fashion_Holiday_Female": "T-Icon-Pickaxes-StreetFashionHolidayPickaxe.png",
    "Tiramisu_Male_Axe_R": "T-Icon-Pickaxes-TiramisuPickaxe.png",
    "TotalFlock_Axe": "T_Icon_Pickaxes_TotalFlock_L.png",
    "TroutWrist_Axe": "TIconPickaxesTroutWrist.png",
    "SK_Pickaxe_Basic": "T-Icon-Weapons-SK-Pickaxe-Basic.png",
}
WEAPON_MISSING = {
    "GenericTwoHandedSword_01": "Creative-mode prop, not a cosmetic: no icon exists",
    "SK_Pickaxe_Durrburger": "Only DurrburgerKnight/DurrburgerSkull icons were supplied; they are other cosmetics",
}
# Emotes without a cosmetic id in the registry: the stock Fortnite icon names.
EMOTE_ALIASES = {"DanceMoves": "T-Icon-Emotes-E-Dance.png", "Floss": "T-Icon-Emotes-E-Floss.png",
                 "Dab": "T-Icon-Emotes-E-Dab.png"}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def norm(value):
    value = value.lower().replace("female", "f").replace("male", "m")
    return re.sub(r"[^a-z0-9]", "", value)


def strip_prefix(value):
    for prefix in ("skpickaxe", "smpickaxe", "pickaxe", "sk"):
        if value.startswith(prefix):
            return value[len(prefix):]
    return value


def icon_key(name):
    value = re.sub(r"\.png$", "", name, flags=re.I)
    value = re.sub(r"^t[-_]?(ui[-_])?icon[-_]?pickaxes?[-_]", "", value, flags=re.I)
    return re.sub(r"(1h)?(pickaxes?|axe)$", "", strip_prefix(norm(value)))


def weapon_keys(row):
    parts = row["source"].split("/")
    names = [parts[-1].replace(".uemodel", ""), parts[-3] if len(parts) > 2 else "", row.get("cosmetic_id") or ""]
    keys = set()
    for name in names:
        value = strip_prefix(norm(re.sub(r"^pickaxe_id_\d+_", "", name, flags=re.I)))
        value = re.sub(r"(1h)?(pickaxes?|axe)(l|r)?$", lambda m: m.group(3) or "", value)
        if value and value != "meshes":
            keys.add(value)
    return keys


def weapons():
    rows = {}
    for path in WEAPON_CATALOGS:
        for row in json.loads(path.read_text()):
            if 3430000 <= int(row.get("display", 0)) < 3440000:
                rows[int(row["display"])] = row
    root = FORTNITE_ICONS / "Pickaxes"
    by_key = {}
    for name in os.listdir(root):
        if name.lower().endswith(".png") and "guitar" not in name.lower():
            by_key.setdefault(icon_key(name), set()).add(name)
    matched, missing = {}, {}
    for display, row in sorted(rows.items()):
        stem = row["source"].split("/")[-1].replace(".uemodel", "")
        if stem in WEAPON_MISSING:
            missing[display] = {"name": row["name"], "reason": WEAPON_MISSING[stem]}
            continue
        if stem in WEAPON_ALIASES:
            matched[display] = (root / WEAPON_ALIASES[stem], row["name"], "Explicit alias, checked against the mesh texture")
            continue
        found = {name for key in weapon_keys(row) for name in by_key.get(key, ())}
        if len(found) != 1:
            raise ValueError(f"{display} {row['name']}: expected one exact icon, found {sorted(found)}")
        matched[display] = (root / found.pop(), row["name"], "Exact normalized mesh/package/cosmetic name")
    for path, *_ in matched.values():
        if not path.is_file():
            raise FileNotFoundError(path)
    return matched, missing


def emotes():
    registry = json.loads(EMOTE_REGISTRY.read_text())["emotes"]
    root = FORTNITE_ICONS / "Dance Icons"
    matched = {}
    for key, emote in registry.items():
        if emote["status"] != "approved":
            continue
        if key in EMOTE_ALIASES:
            name, reason = EMOTE_ALIASES[key], "Stock emote icon name"
        else:
            cosmetic = re.sub(r"^EID_", "", emote["cosmetic_id"])
            cosmetic = re.sub(r"_(Athena|[A-Z0-9]{5})$", "", cosmetic)
            name, reason = f"T-Icon-Emotes-E-{cosmetic}.png", "Exact cosmetic id " + emote["cosmetic_id"]
        if not (root / name).is_file():
            raise FileNotFoundError(f"{key}: {root / name}")
        matched[emote["id"]] = (root / name, emote["display"], reason)
    return matched


def keyed_render(path):
    """A preview render on a flat background, cropped to the model with the background made transparent."""
    image = Image.open(path).convert("RGB")
    bg = image.getpixel((2, 2))
    alpha = Image.new("L", image.size)
    px, ax = image.load(), alpha.load()
    for y in range(image.height):
        for x in range(image.width):
            d = sum(abs(a - b) for a, b in zip(px[x, y], bg))
            ax[x, y] = min(255, d * 6)
    image = image.convert("RGBA")
    image.putalpha(alpha)
    box = alpha.point(lambda v: 255 if v > 24 else 0).getbbox()
    image = image.crop(box)
    side = int(max(image.size) * 1.08)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(image, ((side - image.width) // 2, (side - image.height) // 2))
    return square


def extra_icons():
    """display -> (source image, name, reason, icon name) for pet cloaks and elemental swords."""
    out = {}
    for display, cosmetic in PETS.items():
        path = PET_ICONS / f"{display}.png"
        out[display] = (Image.open(path), f"pet {display}", "fortnite-api icon " + cosmetic, f"EsteriaBackpack-{display}")
    for display in SWORDS:
        path = SWORD_RENDERS / str(display) / "preview-0.png"
        out[display] = (keyed_render(path), f"sword {display}", "Render of the model (" + path.name + ")",
                        f"EsteriaPickaxe-{display}")
    return out


def blp(path):
    source = path if isinstance(path, Image.Image) else Image.open(path)
    image = source.convert("RGBA").resize((ICON_SIZE, ICON_SIZE), Image.Resampling.LANCZOS)
    png = io.BytesIO()
    image.save(png, "PNG")
    png.seek(0)
    return blp_bytes(png)


def current_registry():
    config = (CLIENT / "Data/Retail/RetailCasc.ini").read_text()
    values = dict(line.split("=", 1) for line in config.splitlines() if "=" in line and not line.startswith("#"))
    return CLIENT / "Data/Retail" / values["tables"], config, values


def patch_registry(registry, display_icons):
    displays, pool = native.wdbc((registry / "itemdisplayinfo.dbc").read_bytes(), 25)
    items, _ = native.wdbc((registry / "item.dbc").read_bytes(), 8)
    item_by_id = {row[0]: row for row in items}
    display_by_id = {row[0]: index for index, row in enumerate(displays)}
    # strings: keep the whole original pool, append the new icon names after it
    strings = bytearray(pool)

    def add(text):
        offset = len(strings)
        strings.extend(text.encode() + b"\0")
        return offset

    # Crate/key rows from an earlier install keep their place and only get the icon set again.
    display_icons = dict(display_icons)
    for entry in CRATES:
        if entry in display_by_id:
            display_icons[entry] = f"EsteriaCrate-{entry}"
    new_displays = [row.copy() for row in displays]
    for display, icon in display_icons.items():
        new_displays[display_by_id[display]][5] = add(icon)
    new_items = [row.copy() for row in items]
    for entry in CRATES:
        if entry in display_by_id:
            continue
        item = ITEM_ROW.copy()
        row = [0] * 25
        row[0], row[5] = entry, add(f"EsteriaCrate-{entry}")
        new_displays.append(row)
        item[0], item[5] = entry, entry
        new_items.append(item)

    def pack(fields, rows, pool_bytes):
        return (struct.pack("<4s4I", b"WDBC", len(rows), fields, fields * 4, len(pool_bytes)) +
                b"".join(struct.pack(f"<{fields}I", *row) for row in rows) + bytes(pool_bytes))

    display_data = pack(25, new_displays, strings)
    after, after_pool = native.wdbc(display_data, 25)
    assert after_pool[:len(pool)] == pool
    for before, now in zip(displays, after):
        if before[0] in display_icons:
            assert before[:5] == now[:5] and before[6:] == now[6:]
        else:
            assert before == now
    _, item_pool = native.wdbc((registry / "item.dbc").read_bytes(), 8)
    item_data = pack(8, new_items, item_pool) if len(new_items) != len(items) else (registry / "item.dbc").read_bytes()
    assert native.wdbc(item_data, 8)[0][:len(items)] == items
    return display_data, item_data


def lua_icon_updates(weapon_icons, emote_ids, backpack_icons):
    """Rewrites only the icon strings of matched rows in the generated addon data."""
    path = REPO / "Addons/EsteriaCosmetics/EsteriaCollectionsData.lua"
    text = path.read_text(encoding="utf-8")
    # Collections lists only weapons still on offer; the DBC icon covers the rest.
    present = {int(d) for d in re.findall(r'kind="weapon",id=\d+,display=(\d+)', text)}
    expected = [display for display in weapon_icons if display in present]
    changed = 0
    for display, icon in weapon_icons.items():
        pattern = re.compile(r'(\{kind="weapon",id=\d+,display=' + str(display) + r',[^\n]*?icon=")[^"]*(")')
        text, n = pattern.subn(lambda m: m.group(1) + "Interface\\\\Icons\\\\" + icon + m.group(2), text)
        changed += n
    if changed != len(expected):
        raise ValueError(f"Expected {len(expected)} weapon rows in EsteriaCollectionsData.lua, changed {changed}")
    collections = text
    path = REPO / "Addons/EsteriaCosmetics/EsteriaCosmeticsData.lua"
    text = path.read_text(encoding="utf-8")
    for display, icon in backpack_icons.items():
        pattern = re.compile(r'(\{id=' + str(display) + r',[^\n]*?icon=")[^"]*(")')
        text, n = pattern.subn(lambda m: m.group(1) + "Interface\\\\Icons\\\\" + icon + m.group(2), text)
        if n != 1:
            raise ValueError(f"Backpack {display} not found once in EsteriaCosmeticsData.lua")
    cosmetics = text
    path = REPO / "Addons/EsteriaEmotes/EsteriaEmotesData.lua"
    text = path.read_text(encoding="utf-8")
    for emote in emote_ids:
        pattern = re.compile(r'(\{ ' + str(emote) + r', "[^"]*", "[^"]*", ")[^"]*(")')
        text, n = pattern.subn(lambda m: m.group(1) + "Interface\\\\Icons\\\\EsteriaEmote-" + str(emote) + m.group(2), text)
        if n != 1:
            raise ValueError(f"Emote {emote} not found once in EsteriaEmotesData.lua")
    return collections, text, cosmetics


def stage(args):
    if args.out.exists():
        raise FileExistsError(args.out)
    storm = cars_mount_pack.Storm(STORM_DLL)
    weapon_matches, weapon_missing = weapons()
    emote_matches = emotes()
    files, report = {}, {"crates": {}, "weapons": {}, "emotes": {}, "weapons_missing": weapon_missing}
    for entry, source in CRATES.items():
        key = f"Interface{B}Icons{B}EsteriaCrate-{entry}.blp"
        files[key] = blp(source)
        report["crates"][entry] = {"source": str(source), "icon": key}
    weapon_icons = {}
    for display, (source, name, reason) in weapon_matches.items():
        weapon_icons[display] = f"EsteriaPickaxe-{display}"
        key = f"Interface{B}Icons{B}EsteriaPickaxe-{display}.blp"
        files[key] = blp(source)
        report["weapons"][display] = {"name": name, "source": source.name, "reason": reason}
    extras = extra_icons()
    report["extras"] = {}
    for display, (image, name, reason, icon) in extras.items():
        files[f"Interface{B}Icons{B}{icon}.blp"] = blp(image)
        report["extras"][display] = {"name": name, "reason": reason, "icon": icon}
    for emote, (source, name, reason) in emote_matches.items():
        key = f"Interface{B}Icons{B}EsteriaEmote-{emote}.blp"
        files[key] = blp(source)
        report["emotes"][emote] = {"name": name, "source": source.name, "reason": reason}

    registry, config, values = current_registry()
    display_icons = {**weapon_icons, **{d: e[3] for d, e in extras.items()}}
    display_data, item_data = patch_registry(registry, display_icons)
    revision = "esteria-icons-" + sha(display_data + item_data)[:12]
    args.out.mkdir(parents=True)
    shutil.copytree(registry, args.out / "registry")
    (args.out / "registry/itemdisplayinfo.dbc").write_bytes(display_data)
    (args.out / "registry/item.dbc").write_bytes(item_data)

    archive = args.out / ARCHIVE
    storm.create_archive(archive, files)
    handle = storm.open_archive(archive)
    for name, data in files.items():
        assert storm.read(handle, name) == data, name

    collections_lua, emotes_lua, cosmetics_lua = lua_icon_updates(
        {**weapon_icons, **{d: e[3] for d, e in extras.items() if d in SWORDS}}, list(emote_matches),
        {d: e[3] for d, e in extras.items() if d in PETS})
    (args.out / "EsteriaCollectionsData.lua").write_text(collections_lua, encoding="utf-8", newline="\n")
    (args.out / "EsteriaCosmeticsData.lua").write_text(cosmetics_lua, encoding="utf-8", newline="\n")
    (args.out / "EsteriaEmotesData.lua").write_text(emotes_lua, encoding="utf-8", newline="\n")
    (args.out / "contact-sheet.png").write_bytes(contact_sheet(weapon_matches, emote_matches))

    receipt = {
        "revision": revision, "registry_base": str(registry), "config_before": config,
        "archive": str(archive), "archive_entries": {k: sha(v) for k, v in files.items()},
        "registry_files": {"itemdisplayinfo.dbc": sha(display_data), "item.dbc": sha(item_data)},
        "counts": {"crates": len(report["crates"]), "weapons": len(weapon_matches),
                   "weapons_missing": len(weapon_missing), "emotes": len(emote_matches)},
        **report, "state": "staged and archive read back; not installed",
    }
    (args.out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt["counts"], indent=2), "\n", args.out)


def contact_sheet(weapon_matches, emote_matches):
    from PIL import ImageDraw
    rows = [(f"{d} {n}", s) for d, (s, n, _) in weapon_matches.items()] + \
           [(f"{e} {n}", s) for e, (s, n, _) in emote_matches.items()]
    cols = 10
    sheet = Image.new("RGB", (cols * 130, ((len(rows) + cols - 1) // cols) * 150), (24, 24, 30))
    draw = ImageDraw.Draw(sheet)
    for i, (label, source) in enumerate(rows):
        icon = Image.open(source).convert("RGBA").resize((96, 96))
        x, y = (i % cols) * 130, (i // cols) * 150
        sheet.paste(icon, (x + 17, y + 6), icon)
        draw.text((x + 3, y + 108), label[:21], fill=(220, 220, 220))
        draw.text((x + 3, y + 122), label[21:42], fill=(220, 220, 220))
    out = io.BytesIO()
    sheet.save(out, "PNG")
    return out.getvalue()


def install(args):
    stage_dir = args.stage
    receipt = json.loads((stage_dir / "receipt.json").read_text())
    running = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
    if "wow.exe" in running or "wowclient" in running:
        raise SystemExit("Close WoW first: the client holds its archives open.")
    registry, config, values = current_registry()
    if config != receipt["config_before"]:
        raise SystemExit("RetailCasc.ini changed since staging; stage again.")
    storm = cars_mount_pack.Storm(STORM_DLL)
    backup = stage_dir / "install-backup"
    backup.mkdir(exist_ok=False)
    shutil.copy2(CLIENT / "Data/Retail/RetailCasc.ini", backup / "RetailCasc.ini")

    target = CLIENT / "Data/Retail/Registries" / receipt["revision"]
    shutil.copytree(stage_dir / "registry", target)
    lines = []
    for line in config.splitlines():
        if line.startswith("tables="):
            line = "tables=Registries/" + receipt["revision"]
        elif line.startswith("aliases=") and line.endswith("/aliases.csv"):
            line = "aliases=Registries/" + receipt["revision"] + "/aliases.csv"
        lines.append(line)
    (CLIENT / "Data/Retail/RetailCasc.ini").write_text("\n".join(lines) + "\n", encoding="utf-8")
    shutil.copy2(receipt["archive"], CLIENT / "Data" / ARCHIVE)

    # Edited addon files go back into the archives that own them.
    addon = f"Interface{B}AddOns{B}"
    owners = {
        "Patch-ZzzzzzzzzzzzzUnifiedCollections.MPQ": {
            addon + f"EsteriaCosmetics{B}EsteriaCosmetics.lua": (REPO / "Addons/EsteriaCosmetics/EsteriaCosmetics.lua").read_bytes(),
            addon + f"EsteriaCosmetics{B}EsteriaCollectionsData.lua": (stage_dir / "EsteriaCollectionsData.lua").read_bytes(),
        },
        "Patch-ZzzzzzzzzzzEsteriaDanceUI.MPQ": {
            addon + f"EsteriaEmotes{B}EsteriaEmotesData.lua": (stage_dir / "EsteriaEmotesData.lua").read_bytes(),
        },
        "Patch-ZzzzzzzzzzzFortniteBackpacks.MPQ": {
            addon + f"EsteriaEmotes{B}EsteriaEmotesData.lua": (stage_dir / "EsteriaEmotesData.lua").read_bytes(),
            addon + f"EsteriaCosmetics{B}EsteriaCosmeticsData.lua": (stage_dir / "EsteriaCosmeticsData.lua").read_bytes(),
        },
    }
    for name, entries in owners.items():
        path = CLIENT / "Data" / name
        shutil.copy2(path, backup / name)
        storm.replace_archive_entries(path, entries)
        handle = storm.open_archive(path)
        for key, data in entries.items():
            assert storm.read(handle, key) == data, (name, key)
    # The repo is the source of truth for the generated Lua: keep it in step with what was installed.
    shutil.copy2(stage_dir / "EsteriaCollectionsData.lua", REPO / "Addons/EsteriaCosmetics/EsteriaCollectionsData.lua")
    shutil.copy2(stage_dir / "EsteriaEmotesData.lua", REPO / "Addons/EsteriaEmotes/EsteriaEmotesData.lua")
    shutil.copy2(stage_dir / "EsteriaCosmeticsData.lua", REPO / "Addons/EsteriaCosmetics/EsteriaCosmeticsData.lua")
    receipt["state"] = "installed; backups in install-backup"
    receipt["installed_registry"] = str(target)
    (stage_dir / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("installed", receipt["revision"], "backups:", backup)


def server_sql(args):
    """The server names inventory icons (the Esteria Inventory window) from its own ItemDisplayInfo: its DBC plus
    acore_world.itemdisplayinfo_dbc. Writes icon-only rows for every display that exists only in the client registry."""
    env = {**os.environ, "MSYS_NO_PATHCONV": "1"}
    dbc = args.out.with_suffix(".server-itemdisplayinfo.dbc")
    subprocess.run(["docker", "cp", "ac-worldserver:/azerothcore/env/dist/data/dbc/ItemDisplayInfo.dbc", str(dbc)],
                   check=True, env=env)
    server, _ = native.wdbc(dbc.read_bytes(), 25)
    dbc.unlink()
    # Existing table rows carry full display data (models, textures): never replace them, only fix their icon.
    table = {}
    dump = subprocess.run(["docker", "exec", "ac-database", "mysql", "-uroot", "-ppassword", "-N", "-B", "-e",
                           "SELECT ID, IFNULL(InventoryIcon_1, '') FROM acore_world.itemdisplayinfo_dbc"],
                          check=True, capture_output=True, text=True, env=env).stdout
    for line in dump.splitlines():
        ident, _, icon = line.partition("\t")
        table[int(ident)] = icon
    known = {row[0] for row in server}
    registry, _, _ = current_registry()
    rows, pool = native.wdbc((registry / "itemdisplayinfo.dbc").read_bytes(), 25)

    def text(offset):
        return pool[offset:pool.index(b"\0", offset)].decode("utf-8", "replace")

    def quoted(icon):
        return "'" + icon.replace("\\", "\\\\").replace("'", "''") + "'"

    wanted = [(row[0], text(row[5])) for row in rows if row[0] not in known and text(row[5])]
    missing = sorted((d, icon) for d, icon in wanted if d not in table)
    changed = sorted((d, icon) for d, icon in wanted if d in table and table[d] != icon)
    lines = [f"-- ItemDisplayInfo icons for displays the server's DBC lacks, from the client registry ({registry.name}),",
             "-- so server-built inventory rows (Esteria Inventory) name the right icon. Rows already in the table keep",
             "-- their display data; only InventoryIcon_1 is updated. Generated by",
             "-- wowdecomp/Tools/CosmeticsPack/stage_esteria_icons.py server-sql; regenerate after registry changes."]
    for display, icon in changed:
        lines.append(f"UPDATE `itemdisplayinfo_dbc` SET `InventoryIcon_1` = {quoted(icon)} WHERE `ID` = {display};")
    for start in range(0, len(missing), 500):
        chunk = missing[start:start + 500]
        lines.append("DELETE FROM `itemdisplayinfo_dbc` WHERE `ID` IN (" + ",".join(str(d) for d, _ in chunk) + ");")
        lines.append("INSERT INTO `itemdisplayinfo_dbc` (`ID`, `InventoryIcon_1`) VALUES")
        lines.append(",\n".join("(%d,'%s')" % (d, icon.replace("\\", "\\\\").replace("'", "''")) for d, icon in chunk) + ";")
    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"{args.out}: {len(missing)} new icon-only rows, {len(changed)} icon updates")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    s = sub.add_parser("stage")
    s.add_argument("--out", type=Path, required=True)
    i = sub.add_parser("install")
    i.add_argument("stage", type=Path)
    q = sub.add_parser("server-sql", help="icon rows the server is missing, as a world SQL update")
    q.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    {"stage": stage, "install": install, "server-sql": server_sql}[args.command](args)


if __name__ == "__main__":
    main()
