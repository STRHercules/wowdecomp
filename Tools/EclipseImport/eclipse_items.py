"""Import every Eclipse item appearance Esteria does not have as an equippable item (usable for transmog).

  stage   one item + one ItemDisplayInfo row per realm display whose look (models, textures, body textures, geosets)
          Esteria lacks; the client rows go in one client-only registry pack, the server rows in batched SQL
  pack    patch-EclipseItems.MPQ: every eclipse.mpq file Esteria lacks (eclipselegacy\\ holds Eclipse's copies of
          files Esteria already has, so it is left out: Esteria's files win)
  install-client | install-server

Sources: wardrobe/rows.edb (IDR1: the realm's baked ItemDisplayInfo rows), items.edb (EIDB: realm item -> display,
class, subclass, inventory type) and names.edb (ENDB: item names).
"""
from __future__ import annotations

import argparse
import bisect
import json
import re
import sqlite3
import struct
from collections import Counter
from contextlib import closing
from pathlib import Path

import eclipse_data as ed
import eclipse_install as ei
import native_import as n
from server import query, snapshot

ARCHIVE = "patch-EclipseItems.MPQ"
SOURCE_ARCHIVE = ed.ECLIPSE / "packs/eclipse.mpq"
OBJECT_DIRS = {1: "Head", 3: "Shoulder", 14: "Shield", 16: "Cape", 23: "Weapon", 24: "Ammo", 27: "Quiver"}
BODY_DIRS = ("ArmUpperTexture", "ArmLowerTexture", "HandTexture", "TorsoUpperTexture", "TorsoLowerTexture",
             "LegUpperTexture", "LegLowerTexture", "FootTexture")
ARMOR_MATERIAL = {0: 4, 1: 7, 2: 8, 3: 5, 4: 6, 6: 1}
WEAPON_SHEATH = {0: 3, 1: 1, 2: 0, 3: 0, 4: 3, 5: 1, 6: 1, 7: 3, 8: 1, 10: 2, 13: 3, 15: 3, 16: 0, 18: 0, 19: 0, 20: 2}
TWO_HAND = {1, 5, 6, 8, 10, 20}
STRING_COLS = tuple(range(1, 7)) + tuple(range(15, 23))


def realm_tables():
    data = (ed.ECLIPSE / "data/wardrobe/rows.edb").read_bytes()
    magic, version, count, _, stride = struct.unpack_from("<4s4I", data)
    if magic != b"IDR1" or version != 1 or stride != 108:
        raise ValueError("rows.edb is not an IDR1 v1 table")
    arena = data[20 + count * stride:]
    displays = {}
    for i in range(count):
        row = list(struct.unpack_from("<27I", data, 20 + i * stride))
        fields = row[2:]
        displays[row[0]] = {"inv": row[1], "row": fields, "strings": {c: ed.cstr(arena, fields[c]) for c in STRING_COLS}}
    data = (ed.ECLIPSE / "data/wardrobe/items.edb").read_bytes()
    magic, version, count, stride, _ = struct.unpack_from("<4s4I", data)
    if magic != b"EIDB" or version != 2 or stride != 76:
        raise ValueError("items.edb is not an EIDB v2 table")
    items = []
    for i in range(count):
        r = struct.unpack_from("<2I4I8II5H6B", data, 20 + i * stride)
        items.append({"item": r[0], "display": r[1], "inv": r[20], "class": r[24], "subclass": r[25]})
    data = (ed.ECLIPSE / "data/wardrobe/names.edb").read_bytes()
    magic, version, count, stride, _ = struct.unpack_from("<4s4I", data)
    pool = data[20 + count * stride:]
    names = {struct.unpack_from("<I", data, 20 + i * stride)[0]: ed.cstr(pool, struct.unpack_from("<2I", data, 20 + i * stride)[1])
             for i in range(count)}
    return displays, items, names


def look_key(strings: dict, row: list) -> tuple:
    """What a display draws: its file names and geoset/helmet columns (IDs and icons excluded)."""
    return (tuple(strings[c].lower().rsplit(".", 1)[0] for c in (1, 2, 3, 4)),
            tuple(strings[c].lower() for c in range(15, 23)), tuple(row[7:11]), tuple(row[13:15]))


class Files:
    """Every path the client could open: Esteria's archives plus the Eclipse files we will ship."""

    def __init__(self, names):
        self.names = sorted(set(names))

    def prefixed(self, prefix: str) -> bool:
        i = bisect.bisect_left(self.names, prefix)
        return i < len(self.names) and self.names[i].startswith(prefix)

    def drawable(self, inv: int, strings: dict) -> bool:
        folder = OBJECT_DIRS.get(inv, "Weapon" if inv in (13, 15, 17, 21, 22, 25, 26) else None)
        for c in (1, 2):
            stem = strings[c].lower().rsplit(".", 1)[0]
            if stem and not (folder and self.prefixed(f"item\\objectcomponents\\{folder.lower()}\\{stem}")):
                return False
        for c in (3, 4):
            stem = strings[c].lower()
            if stem and not (folder and self.prefixed(f"item\\objectcomponents\\{folder.lower()}\\{stem}")):
                return False
        for region, c in zip(BODY_DIRS, range(15, 23)):
            stem = strings[c].lower()
            if stem and not self.prefixed(f"item\\texturecomponents\\{region.lower()}\\{stem}"):
                return False
        return bool(any(strings[c] for c in (1, 2, 15, 16, 17, 18, 19, 20, 21, 22)))


def allocate_block(path: Path, kind: str, keys: list[str], used: set[int], start: int) -> dict[str, int]:
    with closing(sqlite3.connect(path, timeout=30)) as db, db:
        db.execute("CREATE TABLE IF NOT EXISTS eclipse_allocations (source_key TEXT, kind TEXT, id INTEGER, "
                   "PRIMARY KEY(source_key, kind), UNIQUE(kind, id))")
        db.execute("BEGIN IMMEDIATE")
        result = dict(db.execute("SELECT source_key, id FROM eclipse_allocations WHERE kind=?", (kind,)))
        taken = used | set(result.values())
        for key in keys:
            if key in result:
                continue
            while start in taken:
                start += 1
            if start > 5000000:
                raise ValueError(f"{kind}: ID budget exhausted")
            db.execute("INSERT INTO eclipse_allocations VALUES (?,?,?)", (key, kind, start))
            result[key] = start
            taken.add(start)
        return result


def stage(args):
    packs = args.stage / "packs"
    if packs.exists():
        raise SystemExit(f"{packs} already exists; stage into a new directory")
    displays, items, names = realm_tables()
    config = dict(line.strip().split("=", 1) for line in (args.client / "Data/Retail/RetailCasc.ini").read_text().splitlines() if "=" in line)
    product, build = config["product"], config["build"]
    client, providers = n.client_ids(args.client, args.server_root)
    server_ids, server_providers = n.server_table_ids()
    schema = snapshot()
    archives = ed.Archives(args.client)
    esteria_names = archives.names()
    owned = set()
    for row, pool in archives.tables("ItemDisplayInfo").values():
        owned.add(look_key({c: ed.cstr(pool, row[c]) for c in STRING_COLS}, row))
    icon_ok = {ed.stem(nm) for nm in esteria_names if nm.startswith("interface\\icons\\")}
    source = ed.storm()
    handle = source.open_archive(SOURCE_ARCHIVE)
    eclipse_names = [ed.norm(nm) for nm in ed.list_files(source, handle) if not nm.startswith("(")]
    source.dll.SFileCloseArchive(handle)
    shipped = [nm for nm in eclipse_names if not nm.startswith("eclipselegacy\\") and nm not in esteria_names]
    # Archives without a listfile (common-3.MPQ) answer by name only.
    shipped = [nm for nm in shipped if not archives.has(nm)]
    archives.close()
    files = Files(list(esteria_names) + shipped)
    icons = ed.Icons()
    by_display = {}
    for item in items:
        by_display.setdefault(item["display"], []).append(item)
    stats, chosen, seen = Counter(), [], set()
    for realm_display in sorted(by_display):
        display = displays.get(realm_display)
        if not display:
            stats["no display row"] += 1
            continue
        key = look_key(display["strings"], display["row"])
        if key in owned:
            stats["already in Esteria"] += 1
            continue
        if key in seen:
            stats["duplicate look"] += 1
            continue
        if not files.drawable(display["inv"], display["strings"]):
            stats["files missing"] += 1
            continue
        seen.add(key)
        # The named item of this display with an equippable class, else the first one.
        candidates = sorted(by_display[realm_display], key=lambda it: (it["item"] not in names, it["class"] not in (2, 4), it["item"]))
        item = candidates[0]
        if item["class"] not in (2, 4) or not item["inv"]:
            stats["not equippable"] += 1
            continue
        chosen.append((realm_display, display, item, names.get(item["item"]) or f"Eclipse Appearance {realm_display}"))
        stats["new"] += 1
    keys = [f"{product}:{build}:eclipse-item:{realm_display}" for realm_display, *_ in chosen]
    allocations = args.client / "Data/Retail/allocations.sqlite"
    with closing(sqlite3.connect(allocations)) as db:
        old_items = {r[0] for r in db.execute("SELECT item_id FROM allocations")}
        old_displays = {r[0] for r in db.execute("SELECT display_id FROM allocations")}
    item_used = client["Item"] | server_ids["Item"] | set(schema["item_template"]["ids"]) | set(schema["item_dbc"]["ids"]) | old_items
    display_used = client["ItemDisplayInfo"] | server_ids["ItemDisplayInfo"] | set(schema["itemdisplayinfo_dbc"]["ids"]) | old_displays
    item_ids = allocate_block(allocations, "Item", keys, item_used, 1100000)
    display_ids = allocate_block(allocations, "ItemDisplayInfo", keys, display_used, 200000)
    item_rows, display_rows, display_strings, shipped_icons = [], [], {}, set()
    sql_items, sql_item_dbc, sql_displays, listing = [], [], [], []
    for key, (realm_display, display, item, name) in zip(keys, chosen):
        item_id, display_id = item_ids[key], display_ids[key]
        strings = dict(display["strings"])
        icon = re.sub(r"\.blp$", "", strings[5], flags=re.I)
        if icon.lower() not in icon_ok:
            if icon and icon.isascii() and icon in icons:
                shipped_icons.add(icon)
            else:
                icon = "INV_Misc_QuestionMark"
        strings[5], strings[6] = icon, ""
        row = list(display["row"])
        row[0] = display_id
        row[11] = row[23] = row[24] = 0   # realm spell visual, item visual and particle colour ids
        for c in STRING_COLS:
            row[c] = 0
        index = len(display_rows)
        display_strings.update({(index, c): v for c, v in strings.items() if v})
        display_rows.append(row)
        cls, sub, inv = item["class"], item["subclass"], item["inv"]
        material = ARMOR_MATERIAL.get(sub, 1) if cls == 4 else 1
        sheath = (4 if sub == 6 else 6 if inv == 23 else 0) if cls == 4 else WEAPON_SHEATH.get(sub, 3)
        item_rows.append([item_id, cls, sub, 0xFFFFFFFF, material, display_id, inv, sheath])
        q = lambda s: "'" + s.replace("\\", "\\\\").replace("'", "''") + "'"
        weapon = cls == 2
        sql_items.append(f"({item_id},{cls},{sub},-1,{q(name)},{display_id},4,{inv},-1,-1,1,1,1,{material},{sheath},0,"
                         f"{(3500 if sub in TWO_HAND else 2000) if weapon else 0},{1 if weapon else 0},{2 if weapon else 0},1,12340)")
        sql_item_dbc.append(f"({item_id},{cls},{sub},-1,{material},{display_id},{inv},{sheath})")
        cols = [display_id] + [q(strings[c]) if c in STRING_COLS else str(row[c]) for c in range(1, 25)]
        sql_displays.append("(" + ",".join(str(v) for v in cols) + ")")
        listing.append(f"{item_id};{display_id};{realm_display};{item['item']};{cls};{sub};{inv};{name}")
    packs.mkdir(parents=True)
    pack = packs / "eclipse-items"
    (pack / "tables").mkdir(parents=True)
    (pack / "tables/item.dbc").write_bytes(n.make_wdbc(8, item_rows))
    (pack / "tables/itemdisplayinfo.dbc").write_bytes(n.make_wdbc(25, display_rows, display_strings))
    (pack / "equipment.csv").write_text("", encoding="ascii")
    source_info = {"product": product, "build": build, "identity_kind": "eclipse-items", "asset_bytes": "eclipse-wotlk-mpq"}
    target = {"kind": "client-rows", "name": "Eclipse items", "records": {}, "items": len(item_rows),
              "first_item": min(item_ids.values(), default=0), "last_item": max(item_ids.values(), default=0)}
    manifest = {"schema": 1, "slug": "eclipse-items", "source_key": f"{product}:{build}:eclipse-items", "source": source_info,
                "target": target, "baselines": {"client_providers": providers, "server_providers": server_providers}}
    target["tables_sha256"] = n.digest((pack / "tables/item.dbc").read_bytes() + (pack / "tables/itemdisplayinfo.dbc").read_bytes())
    manifest["definition_sha256"] = n.digest(json.dumps({"source": source_info, "target": target}, sort_keys=True).encode())
    (pack / "server.sql").write_text("", encoding="utf-8")
    manifest["files"] = {p.relative_to(pack).as_posix(): {"sha256": n.digest(p.read_bytes()), "size": p.stat().st_size}
                         for p in sorted(pack.rglob("*")) if p.is_file()}
    n.save_json(pack / "manifest.json", manifest)
    n.load_pack(pack)
    server = args.stage / "server"
    server.mkdir()
    ids = ",".join(str(i) for i in sorted(item_ids.values()))
    dids = ",".join(str(i) for i in sorted(display_ids.values()))
    sql = ["-- Eclipse items: additive; aborts (rolls back) if any target ID is already taken.",
           "CREATE TEMPORARY TABLE `_eclipse_items_guard` (`ok` int NOT NULL CHECK (`ok` = 1));",
           "START TRANSACTION;",
           f"INSERT INTO `_eclipse_items_guard` SELECT 0 FROM `item_template` WHERE `entry` IN ({ids}) LIMIT 1;",
           f"INSERT INTO `_eclipse_items_guard` SELECT 0 FROM `item_dbc` WHERE `ID` IN ({ids}) LIMIT 1;",
           f"INSERT INTO `_eclipse_items_guard` SELECT 0 FROM `itemdisplayinfo_dbc` WHERE `ID` IN ({dids}) LIMIT 1;"]
    template_cols = ("entry,class,subclass,SoundOverrideSubclass,name,displayid,Quality,InventoryType,AllowableClass,"
                     "AllowableRace,ItemLevel,RequiredLevel,bonding,Material,sheath,stackable,delay,dmg_min1,dmg_max1,maxcount,VerifiedBuild")
    display_cols = ",".join(["ID", "ModelName_1", "ModelName_2", "ModelTexture_1", "ModelTexture_2", "InventoryIcon_1", "InventoryIcon_2",
                             "GeosetGroup_1", "GeosetGroup_2", "GeosetGroup_3", "Flags", "SpellVisualID", "GroupSoundIndex",
                             "HelmetGeosetVis_1", "HelmetGeosetVis_2", "Texture_1", "Texture_2", "Texture_3", "Texture_4",
                             "Texture_5", "Texture_6", "Texture_7", "Texture_8", "ItemVisual", "ParticleColorID"])
    for start in range(0, len(sql_items), 500):
        sql.append(f"INSERT INTO `item_template` ({template_cols}) VALUES\n" + ",\n".join(sql_items[start:start + 500]) + ";")
        sql.append("INSERT INTO `item_dbc` (ID,ClassID,SubclassID,Sound_Override_Subclassid,Material,DisplayInfoID,InventoryType,SheatheType) VALUES\n"
                   + ",\n".join(sql_item_dbc[start:start + 500]) + ";")
        sql.append(f"INSERT INTO `itemdisplayinfo_dbc` ({display_cols}) VALUES\n" + ",\n".join(sql_displays[start:start + 500]) + ";")
    sql += ["COMMIT;", "DROP TEMPORARY TABLE `_eclipse_items_guard`;"]
    (server / "eclipse-items.sql").write_text("\n".join(sql) + "\n", encoding="utf-8")
    (args.stage / "assets.txt").write_text("\n".join(sorted(shipped)) + "\n", encoding="latin-1")
    (args.stage / "icons.txt").write_text("\n".join(sorted(shipped_icons)) + "\n", encoding="utf-8")
    (args.stage / "eclipse-items.csv").write_text("item;display;realm_display;realm_item;class;subclass;inventory_type;name\n"
                                                  + "\n".join(listing) + "\n", encoding="utf-8")
    print(json.dumps({**stats, "asset_files": len(shipped), "icons_shipped": len(shipped_icons),
                      "items": f"{target['first_item']}..{target['last_item']}"}, indent=2))


def pack(args):
    names = [nm for nm in (args.stage / "assets.txt").read_text(encoding="latin-1").splitlines() if nm and nm.isascii()]
    icons = ed.Icons()
    extra = {f"Interface\\Icons\\{icon}.blp": icons.blp(icon) for icon in (args.stage / "icons.txt").read_text(encoding="utf-8").split()}
    ei.pack_assets(ed.storm(), [SOURCE_ARCHIVE], names, args.stage / ARCHIVE, extra)
    print(f"{len(names)} files + {len(extra)} icons -> {args.stage / ARCHIVE}")


def install_server(args):
    query((args.stage / "server" / "eclipse-items.sql").read_text(encoding="utf-8"), timeout=3600)
    first, last = (json.loads((args.stage / "packs/eclipse-items/manifest.json").read_text())["target"][k] for k in ("first_item", "last_item"))
    print("item_template rows now in range:", query(f"SELECT COUNT(*) FROM item_template WHERE entry BETWEEN {first} AND {last};"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("stage", "pack", "install-client", "install-server"))
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--client", type=Path, default=ed.CLIENT)
    parser.add_argument("--server-root", type=Path, default=ed.SERVER_TOOLS.parent)
    args = parser.parse_args()
    {"stage": stage, "pack": pack, "install-server": install_server,
     "install-client": lambda a: ei.install_client(a.client, a.stage, ARCHIVE, "eclipse-items")}[args.command](args)
