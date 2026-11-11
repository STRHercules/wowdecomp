"""Import the Eclipse mounts Esteria does not have, the same way Esteria's retail mounts are installed.

  plan     choose the mounts and their files               -> <stage>/plan.json
  stage    allocate IDs, write one client pack per mount   -> <stage>/packs/*, <stage>/server/*, <stage>/assets.txt
  install-client  copy packs into Data/Retail, rebuild the registry, write the asset MPQ (backs up first)
  install-server  apply SQL to acore_world and publish the DBC continuations (needs a worldserver restart)

A mount is "already in Esteria" when an Esteria mount spell (client tables, server creature models, or an installed
retail pack) shows the same model file and the same texture variations, or carries the same name.
"""
from __future__ import annotations

import argparse
import json
import re
import struct
from collections import Counter
from pathlib import Path

import eclipse_data as ed
import eclipse_install as ei
import native_import as n
import native_mount as nm
from server import KEYS, query, snapshot

LISTFILE = Path(r"R:\Users\Zach\Documents\GitHub\wowdecomp\build\skyriding-research\listfile\listfile.csv")
MOUNT_ARCHIVE = ed.ECLIPSE / "packs/mounteclipse.mpq"
FLY = 135                       # AnimationData "Fly"
DONOR_FLYING, DONOR_GROUND = 61309, 16056   # the donors Esteria's retail mounts use
POSE_VISUAL = {484: 14418, 120: 12844}      # SpellVisuals whose kit seats the rider in that pose, already used by mounts
PLAIN_VISUAL = 15809                         # native_mount.py: seats the rider with Mount (91)
SLUG = "eclipse-mounts"
ARCHIVE = "patch-EclipseMounts.MPQ"
STOCK_ARCHIVES = ("common.mpq", "common-2.mpq", "expansion.mpq", "lichking.mpq", "patch.mpq", "patch-2.mpq", "patch-3.mpq")


def retail_mounts(client: Path):
    """(model stem, texture stems) and names of every installed retail mount pack."""
    recipes = [json.loads(p.read_text()) for p in (client / "Data/Retail/Packs").glob("*/resolved-source.json")]
    recipes = [r for r in recipes if "retail_mount_id" in r]
    wanted = {r["model_file_id"] for r in recipes} | {t for r in recipes for t in r["texture_file_ids"]}
    names = {}
    with LISTFILE.open(encoding="utf-8", errors="replace") as f:
        for line in f:
            fid, _, path = line.partition(";")
            if fid.isdigit() and int(fid) in wanted:
                names[int(fid)] = path.strip()
    keys = set()
    for r in recipes:
        if r["model_file_id"] in names:
            textures = tuple(sorted(ed.stem(names[t]) for t in r["texture_file_ids"] if t in names))
            keys.add((ed.stem(names[r["model_file_id"]]), textures))
    return keys, {r["name"].lower() for r in recipes}


def esteria_mounts(archives: ed.Archives, client: Path):
    spells = archives.tables("Spell")
    displays = archives.tables("CreatureDisplayInfo")
    models = archives.tables("CreatureModelData")
    creature_displays = {}
    for creature, display in ed.world_query("SELECT CreatureID, CreatureDisplayID FROM creature_template_model;"):
        creature_displays.setdefault(int(creature), []).append(int(display))
    keys, names = retail_mounts(client)
    for row, pool in spells.values():
        auras = row[95:98]
        if 78 not in auras:
            continue
        names.add(ed.cstr(pool, row[136]).lower())
        for display in creature_displays.get(row[110 + auras.index(78)], []):
            if display in displays and displays[display][0][1] in models:
                drow, dpool = displays[display]
                mrow, mpool = models[drow[1]]
                keys.add((ed.stem(ed.cstr(mpool, mrow[2])), tuple(sorted(ed.stem(ed.cstr(dpool, drow[i])) for i in (6, 7, 8) if drow[i]))))
    names.discard("")
    return keys, names


def display_name(mount):
    name = mount["name"].strip()
    if name.startswith("[PH]"):
        name = re.sub(r"(?<=[a-z])(mount|pvp)", r" \1", name[4:].strip().replace("_", " "), flags=re.I)
        name = " ".join(word.capitalize() for word in name.split())
    return name or "Eclipse Mount"


def plan(args):
    archives = ed.Archives(args.client)
    cdi = ed.Edt(ed.ECLIPSE / "data/tables/CreatureDisplayInfo.edt")
    cmd = ed.Edt(ed.ECLIPSE / "data/tables/CreatureModelData.edt")
    owned_keys, owned_names = esteria_mounts(archives, args.client)
    source = ed.storm()
    eclipse = source.open_archive(MOUNT_ARCHIVE)
    eclipse_names = {ed.norm(name): name for name in ed.list_files(source, eclipse)}
    stats, chosen, seen = Counter(), [], {}
    # Named mounts before [PH] colour placeholders, so a duplicate keeps the real name.
    for mount in sorted(ed.eclipse_mounts(), key=lambda m: (m["name"].startswith("[PH]"), m["name"].lower())):
        display = cdi.rows.get(mount["display"])
        model_row = cmd.rows.get(display[1]) if display else None
        if not model_row:
            stats["no display row"] += 1
            continue
        model = ed.as_m2(cmd.text(model_row[2]))
        textures = [cdi.text(display[i]) for i in (6, 7, 8)]
        key = (ed.stem(model), tuple(sorted(ed.stem(t) for t in textures if t)))
        if key in owned_keys:
            stats["already in Esteria (model + textures)"] += 1
            continue
        if mount["name"].lower() in owned_names:
            stats["already in Esteria (name)"] += 1
            continue
        if key in seen:
            stats["duplicate inside Eclipse"] += 1
            continue
        model_key = ed.norm(model)
        if model_key not in eclipse_names and not archives.has(model):
            stats["model file missing"] += 1
            continue
        seen[key] = mount
        data = source.read(eclipse, eclipse_names[model_key]) if model_key in eclipse_names else None
        folder = model_key.rsplit(ed.BS, 1)[0]
        files = {model_key}
        if data:
            files.update(ed.m2_skins(model, data))
            files.update(ed.m2_external_anims(model, data))
            files.update(ed.norm(t) for t in ed.m2_textures(data))
        files.update(ed.norm(f"{folder}\\{t}.blp") for t in textures if t)
        # Eclipse's packer dropped some external .anim files; the client just skips an animation it cannot open.
        files = {f for f in files if not f.endswith(".anim") or f in eclipse_names or archives.has(f)}
        missing = [f for f in files if f not in eclipse_names and not archives.has(f)]
        if missing:
            stats["files missing"] += 1
            print("skip (missing files):", mount["name"], missing[:3])
            continue
        flying = FLY in ed.m2_sequences(data or archives_read(archives, model))
        chosen.append({**mount, "name": display_name(mount), "eclipse_name": mount["name"], "model_path": model,
                       "textures": textures, "flying": flying,
                       # where Esteria owns the .m2, Eclipse's skins/anims would not match it (felhound401.skin crash)
                       "files": sorted(f for f in files if f in eclipse_names and not archives.has(f)
                                       and ((data is not None and not archives.has(model)) or not f.endswith((".skin", ".anim"))))})
        stats["new"] += 1
    source.dll.SFileCloseArchive(eclipse)
    # Unique names for the journal and the learn list.
    counts = Counter(m["name"] for m in chosen)
    for m in chosen:
        if counts[m["name"]] > 1:
            m["name"] = f"{m['name']} ({ed.stem(m['textures'][0]).split('_')[-1].capitalize() if m['textures'][0] else m['display']})"
    counts = Counter(m["name"] for m in chosen)
    for m in chosen:
        if counts[m["name"]] > 1:
            m["name"] = f"{m['name']} {m['display']}"
    args.stage.mkdir(parents=True, exist_ok=True)
    ed_files = sorted({f for m in chosen for f in m["files"]})
    n.save_json(args.stage / "plan.json", {"mounts": chosen, "stats": stats, "asset_files": ed_files})
    archives.close()
    print(json.dumps(dict(stats), indent=2), f"\nflying {sum(m['flying'] for m in chosen)}, ground {sum(not m['flying'] for m in chosen)}, asset files {len(ed_files)}")


def archives_read(archives: ed.Archives, name: str) -> bytes:
    for handle in reversed(list(archives.handles.values())):
        try:
            return archives.storm.read(handle, name.replace("/", ed.BS))
        except OSError:
            continue
    raise FileNotFoundError(name)


def slugify(name: str, used: set[str]) -> str:
    base = "eclipse-" + (re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "mount")
    base = base[:56].rstrip("-")
    slug, i = base, 2
    while slug in used:
        slug, i = f"{base}-{i}", i + 1
    used.add(slug)
    return slug


def stock_ids(archives: ed.Archives, table: str) -> set[int]:
    ids = set()
    for path, handle in archives.handles.items():
        if path.name.lower() in STOCK_ARCHIVES:
            try:
                payload = archives.storm.read(handle, f"DBFilesClient\\{table}.dbc")
            except OSError:
                continue
            _, count, fields, size, _ = struct.unpack_from("<4s4I", payload)
            ids.update(struct.unpack_from("<I", payload, 20 + i * size)[0] for i in range(count))
    return ids


def stage(args):
    plan_data = json.loads((args.stage / "plan.json").read_text())
    mounts = plan_data["mounts"]
    packs = args.stage / "packs"
    if packs.exists():
        raise SystemExit(f"{packs} already exists; stage into a new directory")
    config = dict(line.split("=", 1) for line in (args.client / "Data/Retail/RetailCasc.ini").read_text().splitlines() if "=" in line)
    product, build = config["product"].strip(), config["build"].strip()
    cdi = ed.Edt(ed.ECLIPSE / "data/tables/CreatureDisplayInfo.edt")
    cmd = ed.Edt(ed.ECLIPSE / "data/tables/CreatureModelData.edt")
    archives = ed.Archives(args.client)
    # Realm references survive only when they mean the same thing in stock 3.3.5.
    stock = {t: stock_ids(archives, t) for t in ("CreatureSoundData", "UnitBlood", "FootprintTextures", "NPCSounds", "ParticleColor")}
    eclipse_icons = ed.Icons()
    icons = {}  # icon -> "esteria" | "eclipse" | None
    for m in mounts:
        m["icon"] = re.sub(r"\.blp$", "", m["icon"], flags=re.I)
        if m["icon"] and m["icon"].isascii() and m["icon"] not in icons:
            icons[m["icon"]] = ("esteria" if archives.has(f"Interface\\Icons\\{m['icon']}.blp")
                                else "eclipse" if m["icon"] in eclipse_icons else None)
    archives.close()
    client, providers = n.client_ids(args.client, args.server_root)
    server_ids, server_providers = n.server_table_ids()
    schema = snapshot(KEYS)
    used = {table: client[table] | server_ids[table] for table in n.TABLES}
    for table, sql_table in nm.SQL_DBC.items():
        used[table] |= {int(v) for v in query(f"SELECT ID FROM `{sql_table}`;").splitlines()}
    used["Creature"] = set(schema["creature_template"]["ids"]) | set(schema["creature_template_model"]["ids"])
    used["CreatureDisplayInfo"] |= set(schema["creature_model_info"]["ids"])
    item_used = used["Item"] | set(schema["item_template"]["ids"]) | set(schema["item_dbc"]["ids"])
    display_used = used["ItemDisplayInfo"] | set(schema["itemdisplayinfo_dbc"]["ids"])
    donors = {}
    spell_bytes = nm.docker_bytes("cat", f"{nm.SERVER_DATA}/dbc/Spell.dbc")
    rows, pool = n.wdbc(spell_bytes, 234)
    for donor in (DONOR_FLYING, DONOR_GROUND):
        if query(f"SELECT COUNT(*) FROM spell_dbc WHERE ID={donor};") != "0":
            raise ValueError(f"Donor {donor} has a SQL override; resolve it before staging")
        row = next(r for r in rows if r[0] == donor)
        if row[95] != 78:
            raise ValueError(f"Donor {donor} is not a mounted aura")
        donors[donor] = row
    allocations = args.client / "Data/Retail/allocations.sqlite"
    slugs = {p.name for p in (args.client / "Data/Retail/Packs").iterdir()}
    server_tables = {table: [] for table in nm.SQL_DBC}
    server_strings = {table: {} for table in nm.SQL_DBC}
    learn = []
    shipped_icons = set()
    packs.mkdir(parents=True)
    for mount in mounts:
        key = f"{product}:{build}:eclipse-mount:{mount['display']}"
        item, item_display = n.allocate(allocations, key, item_used, display_used)
        ids = nm.allocate_mount(allocations, key, used)
        for kind, value in ids.items():
            used[kind].add(value)
        item_used.add(item)
        display_used.add(item_display)
        name = mount["name"]
        slug = slugify(name, slugs)
        source_display = cdi.rows[mount["display"]]
        source_model = cmd.rows[source_display[1]]
        donor = DONOR_FLYING if mount["flying"] else DONOR_GROUND
        spell = donors[donor].copy()
        spell[0], spell[110], spell[133] = ids["Spell"], ids["Creature"], ids["SpellIcon"]
        spell[131], spell[132] = POSE_VISUAL.get(mount["pose"], PLAIN_VISUAL), 0
        for field in n.TABLES["Spell"][1]:
            spell[field] = 0
        spell[152], spell[169], spell[186], spell[203] = 16712190, 0, 16712190, 16712188
        model = source_model[:28]
        model[0] = ids["CreatureModelData"]
        model[5] = model[5] if model[5] in stock["UnitBlood"] else 0
        model[6] = model[6] if model[6] in stock["FootprintTextures"] else 0
        model[13] = model[13] if model[13] in stock["CreatureSoundData"] else 0
        display = source_display[:16]
        display[0], display[1], display[3] = ids["CreatureDisplayInfo"], ids["CreatureModelData"], 0
        display[2] = display[2] if display[2] in stock["CreatureSoundData"] else 0
        display[11] = display[11] if display[11] in stock["UnitBlood"] else 0
        display[12] = display[12] if display[12] in stock["NPCSounds"] else 0
        display[13] = display[13] if display[13] in stock["ParticleColor"] else 0
        display[15] = 0
        icon = mount["icon"] if icons.get(mount["icon"]) else "Ability_Mount_RidingHorse"
        if icons.get(icon) == "eclipse":
            shipped_icons.add(icon)
        description = "Summons and dismisses this mount."
        speed = "Increases flight speed by $s2%." if mount["flying"] else "Increases speed by $s2%."
        tables = {
            "Item": ([[item, 15, 5, 0xFFFFFFFF, 4, item_display, 0, 0]], {}),
            "ItemDisplayInfo": ([[item_display] + [0] * 24], {(0, 5): icon}),
            "CreatureModelData": ([model], {(0, 2): mount["model_path"]}),
            "CreatureDisplayInfo": ([display], {(0, 6 + i): t for i, t in enumerate(mount["textures"]) if t}
                                    | ({(0, 9): cdi.text(source_display[9])} if source_display[9] else {})),
            "SpellIcon": ([[ids["SpellIcon"], 0]], {(0, 1): f"Interface\\Icons\\{icon}"}),
            "SkillLineAbility": ([[ids["SkillLineAbility"], 777, ids["Spell"], 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0]], {}),
            "Spell": ([spell], {(0, 136): name, (0, 137): name, (0, 170): description, (0, 171): description,
                                (0, 187): speed, (0, 188): speed}),
        }
        records = {
            "item_template": {"entry": item, "class": 15, "subclass": 5, "SoundOverrideSubclass": -1,
                "name": "Reins of the " + name, "displayid": item_display, "Quality": 4, "ItemLevel": 1, "RequiredLevel": 1,
                "AllowableClass": -1, "AllowableRace": -1, "maxcount": 1, "stackable": 1, "bonding": 1,
                "Material": 4, "BagFamily": 128, "description": "Teaches you how to summon the " + name + ".",
                "spellid_1": 483, "spelltrigger_1": 0, "spellcharges_1": -1, "spellcooldown_1": -1,
                "spellcategory_1": 330, "spellcategorycooldown_1": 3000, "spellid_2": ids["Spell"], "spelltrigger_2": 6},
            "item_dbc": dict(zip(("ID", "ClassID", "SubclassID", "Sound_Override_Subclassid", "Material", "DisplayInfoID", "InventoryType", "SheatheType"),
                                 [item, 15, 5, -1, 4, item_display, 0, 0])),
            "itemdisplayinfo_dbc": {"ID": item_display, "InventoryIcon_1": icon},
            "creature_template": {"entry": ids["Creature"], "name": name, "minlevel": 1, "maxlevel": 1, "faction": 35,
                "unit_class": 1, "type": 1, "unit_flags2": 2048, "HoverHeight": 1, "flags_extra": 2, "movementId": 140},
            "creature_template_model": {"CreatureID": ids["Creature"], "Idx": 0, "CreatureDisplayID": ids["CreatureDisplayInfo"],
                "DisplayScale": 1, "Probability": 1, "VerifiedBuild": 12340},
            "creature_model_info": {"DisplayID": ids["CreatureDisplayInfo"], "BoundingRadius": 1, "CombatReach": 2,
                "Gender": 2, "DisplayID_Other_Gender": 0, "VerifiedBuild": 12340},
        }
        pack = packs / slug
        (pack / "tables").mkdir(parents=True)
        for table, (table_rows, strings) in tables.items():
            (pack / "tables" / f"{table.lower()}.dbc").write_bytes(n.make_wdbc(n.TABLES[table][0], table_rows, strings))
            if table in server_tables:
                index = len(server_tables[table])
                server_tables[table].append(table_rows[0])
                server_strings[table].update({(index, field): value for (_, field), value in strings.items()})
        (pack / "equipment.csv").write_text("", encoding="ascii")
        recipe = {"product": product, "build": build, "name": name, "eclipse_display": mount["display"],
                  "eclipse_name": mount["eclipse_name"], "realm_display": mount["realm_display"], "realm_spell": mount["realm_spell"],
                  "model": mount["model_path"], "textures": mount["textures"], "flying": mount["flying"], "pose": mount["pose"],
                  "summon_donor": donor, "asset_bytes": "eclipse-wotlk-mpq", "identity_kind": "eclipse-mount"}
        n.save_json(pack / "resolved-source.json", recipe)
        manifest = {"schema": 1, "slug": slug, "source_key": key, "source": recipe,
            "target": {"kind": "mount", "name": name, "item_id": item, "display_id": item_display, "ids": ids, "records": records},
            "baselines": {"client_providers": providers, "server_providers": server_providers,
                          "server_schema": {table: value["columns"] for table, value in schema.items()}},
            "limitations": ["Classic (MD20 v264) art from Eclipse's mounteclipse.mpq, shipped in patch-EclipseMounts.MPQ.",
                            f"WotLK summon mechanics use donor spell {donor}.", "Level-one learning item for testing."]}
        manifest["definition_sha256"] = n.digest(json.dumps({"source": manifest["source"], "target": manifest["target"]}, sort_keys=True).encode())
        manifest["files"] = {p.relative_to(pack).as_posix(): {"sha256": n.digest(p.read_bytes()), "size": p.stat().st_size}
                             for p in sorted(pack.rglob("*")) if p.is_file()}
        n.save_json(pack / "manifest.json", manifest)
        (pack / "server.sql").write_text(n.server_sql(manifest), encoding="utf-8")
        learn.append((name, "Flying" if mount["flying"] else "Ground", ids["Spell"], item))
    server = args.stage / "server"
    server.mkdir()
    for table, table_rows in server_tables.items():
        (server / f"{table}.dbc1-native-retail-{SLUG}").write_bytes(n.make_wdbc(n.TABLES[table][0], table_rows, server_strings[table]))
    (server / "eclipse-mounts.sql").write_text("".join((packs / p).joinpath("server.sql").read_text(encoding="utf-8")
                                                       for p in sorted(p.name for p in packs.iterdir())), encoding="utf-8")
    (args.stage / "assets.txt").write_text("\n".join(plan_data["asset_files"]) + "\n", encoding="ascii")
    (args.stage / "icons.txt").write_text("\n".join(sorted(shipped_icons)) + "\n", encoding="utf-8")
    lines = [f"# Eclipse mount test commands\n\n{len(learn)} mounts imported from Eclipse. `.learn <spell>` teaches directly, "
             "`.additem <item>` gives the learning item, `.unlearn <spell>` removes it.\n\n| Mount | Type | Learn | Item |\n|---|---|---|---|"]
    lines += [f"| {name} | {kind} | `.learn {spell}` | `.additem {item}` |" for name, kind, spell, item in sorted(learn)]
    (args.stage / "eclipse-mount-learn-commands.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps({"packs": len(learn), "icons": Counter(str(v) for v in icons.values()),
                      "icons_missing": sorted(k for k, v in icons.items() if not v)}, indent=2))


def pack(args):
    for pack_dir in sorted((args.stage / "packs").iterdir()):
        n.load_pack(pack_dir)  # every staged pack must pass the retail installer's own checks
    names = (args.stage / "assets.txt").read_text(encoding="ascii").splitlines()
    icons = ed.Icons()
    extra = {f"Interface\\Icons\\{icon}.blp": icons.blp(icon)
             for icon in (args.stage / "icons.txt").read_text(encoding="utf-8").split()}
    ei.pack_assets(ed.storm(), [MOUNT_ARCHIVE], names, args.stage / ARCHIVE, extra)
    print(f"{len(names)} files + {len(extra)} icons -> {args.stage / ARCHIVE}")


def install_client(args):
    ei.install_client(args.client, args.stage, ARCHIVE, "eclipse-mounts")


def install_server(args):
    ei.install_server(args.stage, "eclipse-mounts.sql")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("plan", "stage", "pack", "install-client", "install-server"))
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--client", type=Path, default=ed.CLIENT)
    parser.add_argument("--server-root", type=Path, default=ed.SERVER_TOOLS.parent)
    args = parser.parse_args()
    {"plan": plan, "stage": stage, "pack": pack, "install-client": install_client, "install-server": install_server}[args.command](args)
