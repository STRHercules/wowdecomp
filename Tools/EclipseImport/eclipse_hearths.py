"""Import the Eclipse hearthstones Esteria does not have as usable hearthstone items.

Each hearth is a copy of Hearthstone (spell 8690, item 6948) whose SpellVisual is Eclipse's: the visual, kit,
effect-name and model-attach rows travel in the hearth's client pack (the supplemental registry), the models in
patch-EclipseHearths.MPQ, the spell row in a server DBC continuation and the item in guarded SQL.

  stage | pack | install-client | install-server     (same steps as eclipse_mounts.py)
"""
from __future__ import annotations

import argparse
import json
import re
import sqlite3
import struct
from contextlib import closing
from pathlib import Path

import eclipse_data as ed
import eclipse_install as ei
import native_import as n
import native_mount as nm
from server import query, snapshot

ARCHIVE = "patch-EclipseHearths.MPQ"
HEARTH_ARCHIVE = ed.ECLIPSE / "packs/heartheclipse.mpq"
DONOR_SPELL, DONOR_ITEM = 8690, 6948
KIT_FIELDS = (1, 2, 3, 4, 5, 6, 14, 15, 22, 23, 24, 25)   # SpellVisual columns that name a SpellVisualKit
EFFECT_FIELDS = tuple(range(3, 15))                        # SpellVisualKit Head..World effect -> SpellVisualEffectName
OWN = 60000                                                # tools/spells/hearth.py numbers Eclipse's own rows from here
SPELL_STRINGS = tuple(range(136, 152)) + tuple(range(153, 169)) + tuple(range(170, 186)) + tuple(range(187, 203))


def hearths():
    data = (ed.ECLIPSE / "data/hearth/hearth.edb").read_bytes()
    magic, version, count, string_bytes = struct.unpack_from("<4s3I", data)
    if magic != b"ESPL" or version != 3:
        raise ValueError("hearth.edb is not an ESPL v3 catalogue")
    pool = data[16:16 + string_bytes]
    result = []
    for i in range(count):
        record = struct.unpack_from("<37I", data, 16 + string_bytes + i * 148)
        result.append({"index": i, "retail_spell": record[1], "name": ed.cstr(pool, record[3]), "visual": list(record[5:])})
    return result


def allocate(path: Path, key: str, kind: str, used: set[int], start: int) -> int:
    with closing(sqlite3.connect(path, timeout=30)) as db, db:
        db.execute("CREATE TABLE IF NOT EXISTS eclipse_allocations (source_key TEXT, kind TEXT, id INTEGER, "
                   "PRIMARY KEY(source_key, kind), UNIQUE(kind, id))")
        db.execute("BEGIN IMMEDIATE")
        previous = db.execute("SELECT id FROM eclipse_allocations WHERE source_key=? AND kind=?", (key, kind)).fetchone()
        if previous:
            return previous[0]
        taken = used | {r[0] for r in db.execute("SELECT id FROM eclipse_allocations WHERE kind=?", (kind,))}
        while start in taken:
            start += 1
        if start > 5000000:
            raise ValueError(f"{kind}: ID budget exhausted")
        db.execute("INSERT INTO eclipse_allocations VALUES (?,?,?)", (key, kind, start))
        return start


def stage(args):
    packs = args.stage / "packs"
    if packs.exists():
        raise SystemExit(f"{packs} already exists; stage into a new directory")
    config = dict(line.strip().split("=", 1) for line in (args.client / "Data/Retail/RetailCasc.ini").read_text().splitlines() if "=" in line)
    product, build = config["product"], config["build"]
    registry = args.client / "Data/Retail" / config["tables"]
    kit, effect, attach = (ed.Edt(ed.ECLIPSE / f"data/tables/{t}.edt") for t in ("SpellVisualKit", "SpellVisualEffectName", "SpellVisualKitModelAttach"))
    archives = ed.Archives(args.client)
    used = {}
    for table in n.CLIENT_TABLES:
        used[table] = set(archives.tables(table))
        if (registry / f"{table.lower()}.dbc").is_file():
            used[table] |= {row[0] for row in n.wdbc((registry / f"{table.lower()}.dbc").read_bytes(), n.CLIENT_TABLES[table][0])[0]}
    spell_names = {ed.cstr(pool, row[136]).lower() for row, pool in archives.tables("Spell").values()}
    donor_item_row = archives.tables("Item")[DONOR_ITEM][0]
    archives.close()
    client, providers = n.client_ids(args.client, args.server_root)
    server_ids, server_providers = n.server_table_ids()
    schema = snapshot()
    item_names = {name.lower() for name in query("SELECT name FROM item_template;").splitlines()}
    used["Spell"] = client["Spell"] | server_ids["Spell"] | {int(v) for v in query("SELECT ID FROM spell_dbc;").splitlines()}
    allocations = args.client / "Data/Retail/allocations.sqlite"
    with closing(sqlite3.connect(allocations)) as db:
        used["Spell"] |= {r[0] for r in db.execute("SELECT id FROM mount_allocations WHERE kind='Spell'")}
    item_used = client["Item"] | server_ids["Item"] | set(schema["item_template"]["ids"]) | set(schema["item_dbc"]["ids"])
    display_used = client["ItemDisplayInfo"] | server_ids["ItemDisplayInfo"] | set(schema["itemdisplayinfo_dbc"]["ids"])
    spell_bytes = nm.docker_bytes("cat", f"{nm.SERVER_DATA}/dbc/Spell.dbc")
    rows, pool = n.wdbc(spell_bytes, 234)
    if query(f"SELECT COUNT(*) FROM spell_dbc WHERE ID={DONOR_SPELL};") != "0":
        raise ValueError("Hearthstone has a SQL override; resolve it before staging")
    donor = next(r for r in rows if r[0] == DONOR_SPELL)
    donor_strings = {field: pool[donor[field]:pool.index(0, donor[field])].decode("utf-8") for field in SPELL_STRINGS if donor[field]}
    donor_item = dict(zip(*[[c.split("\t")[0] for c in schema["item_template"]["columns"]],
                            query(f"SELECT * FROM item_template WHERE entry={DONOR_ITEM};").split("\t")]))
    starts = {table: max(ids) + 1 for table, ids in used.items() if table != "Spell"}
    starts["Spell"] = 300000
    packs.mkdir(parents=True)
    server_spells, server_strings, learn, slugs = [], {}, [], {p.name for p in (args.client / "Data/Retail/Packs").iterdir()}
    for hearth in hearths():
        name = hearth["name"]
        if name.lower() in item_names or name.lower() in spell_names:
            print("already in Esteria:", name)
            continue
        key = f"{product}:{build}:eclipse-hearth:{hearth['retail_spell']}"
        alloc = lambda kind, source: allocate(allocations, f"{key}:{source}", kind, used[kind], starts[kind])
        kits = sorted({hearth["visual"][f] for f in KIT_FIELDS if hearth["visual"][f]})
        if any(k < OWN or k not in kit.rows for k in kits) or any(v >= OWN for i, v in enumerate(hearth["visual"]) if i and i not in KIT_FIELDS):
            raise ValueError(f"{name}: visual references rows outside Eclipse's own kits")
        kit_map = {k: alloc("SpellVisualKit", f"kit:{k}") for k in kits}
        attaches = [row for row in attach.rows.values() if row[1] in kit_map]
        effects = sorted({kit.rows[k][f] for k in kits for f in EFFECT_FIELDS if kit.rows[k][f]} | {row[2] for row in attaches})
        if any(e < OWN or e not in effect.rows for e in effects):
            raise ValueError(f"{name}: effect rows outside Eclipse's own range")
        effect_map = {e: alloc("SpellVisualEffectName", f"effect:{e}") for e in effects}
        visual_id = alloc("SpellVisual", "visual")
        spell_id = alloc("Spell", "spell")
        item, item_display = n.allocate(allocations, key, item_used, display_used)
        item_used.add(item)
        display_used.add(item_display)
        visual = hearth["visual"].copy()
        visual[0] = visual_id
        for field in KIT_FIELDS:
            if visual[field]:
                visual[field] = kit_map[visual[field]]
        kit_rows = []
        for k in kits:
            row = kit.rows[k][:38]
            row[0] = kit_map[k]
            for field in EFFECT_FIELDS:
                if row[field]:
                    row[field] = effect_map[row[field]]
            row[15] = row[16] = 0  # realm sound / camera-shake ids
            kit_rows.append(row)
        effect_rows, effect_strings = [], {}
        for index, e in enumerate(effects):
            row = effect.rows[e][:7]
            effect_strings[(index, 1)] = effect.text(row[1]) or f"EclipseHearth{e}"
            effect_strings[(index, 2)] = effect.text(row[2])
            row[0] = effect_map[e]
            effect_rows.append(row)
        attach_rows = []
        for row in attaches:
            row = row[:10]
            row[0] = alloc("SpellVisualKitModelAttach", f"attach:{row[0]}")
            row[1], row[2] = kit_map[row[1]], effect_map[row[2]]
            attach_rows.append(row)
        spell = donor.copy()
        spell[0], spell[131] = spell_id, visual_id
        strings = {(0, f): v for f, v in donor_strings.items()} | {(0, 136): name, (0, 137): name}
        for field in SPELL_STRINGS:
            spell[field] = 0
        item_row = donor_item_row.copy()
        item_row[0], item_row[5] = item, item_display
        tables = {
            "Item": ([item_row], {}),
            "ItemDisplayInfo": ([[item_display] + [0] * 24], {(0, 5): "INV_Misc_Rune_01"}),
            "Spell": ([spell], strings),
            "SpellVisual": ([visual], {}),
            "SpellVisualKit": (kit_rows, {}),
            "SpellVisualEffectName": (effect_rows, effect_strings),
            "SpellVisualKitModelAttach": (attach_rows, {}),
        }
        template = {k: v for k, v in donor_item.items() if k in ("class", "subclass", "SoundOverrideSubclass", "Material",
                    "Flags", "bonding", "maxcount", "stackable", "spelltrigger_1", "spellcharges_1", "spellcooldown_1",
                    "spellcategory_1", "spellcategorycooldown_1", "description")}
        template = {k: (v if not re.fullmatch(r"-?\d+", v) else int(v)) for k, v in template.items()}
        records = {
            "item_template": {**template, "entry": item, "name": name, "displayid": item_display, "Quality": 3,
                              "ItemLevel": 1, "spellid_1": spell_id},
            "item_dbc": dict(zip(("ID", "ClassID", "SubclassID", "Sound_Override_Subclassid", "Material", "DisplayInfoID",
                                  "InventoryType", "SheatheType"), [item, *[n_ if n_ < 2**31 else n_ - 2**32 for n_ in item_row[1:5]], item_display, *item_row[6:8]])),
            "itemdisplayinfo_dbc": {"ID": item_display, "InventoryIcon_1": "INV_Misc_Rune_01"},
        }
        slug = "eclipse-" + re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
        if slug in slugs:
            raise ValueError(f"pack {slug} exists")
        slugs.add(slug)
        pack = packs / slug
        (pack / "tables").mkdir(parents=True)
        for table, (table_rows, table_strings) in tables.items():
            fields = ei.ALL_TABLES[table][0]
            (pack / "tables" / f"{table.lower()}.dbc").write_bytes(n.make_wdbc(fields, table_rows, table_strings))
        (pack / "equipment.csv").write_text("", encoding="ascii")
        source = {"product": product, "build": build, "name": name, "eclipse_hearth": hearth["index"],
                  "retail_spell": hearth["retail_spell"], "identity_kind": "eclipse-hearth", "asset_bytes": "eclipse-wotlk-mpq",
                  "donor_spell": DONOR_SPELL, "donor_item": DONOR_ITEM}
        manifest = {"schema": 1, "slug": slug, "source_key": key, "source": source,
                    "target": {"kind": "hearth", "name": name, "item_id": item, "display_id": item_display, "records": records,
                               "ids": {"Spell": spell_id, "SpellVisual": visual_id, "kits": kit_map, "effects": effect_map}},
                    "baselines": {"client_providers": providers, "server_providers": server_providers,
                                  "server_schema": {t: v["columns"] for t, v in schema.items()}},
                    "limitations": ["Shares Hearthstone's cooldown category.", "Eclipse's own visual rows; realm sounds dropped."]}
        manifest["target"]["ids"]["kits"] = {str(k): v for k, v in kit_map.items()}
        manifest["target"]["ids"]["effects"] = {str(k): v for k, v in effect_map.items()}
        manifest["definition_sha256"] = n.digest(json.dumps({"source": source, "target": manifest["target"]}, sort_keys=True).encode())
        manifest["files"] = {p.relative_to(pack).as_posix(): {"sha256": n.digest(p.read_bytes()), "size": p.stat().st_size}
                             for p in sorted(pack.rglob("*")) if p.is_file()}
        n.save_json(pack / "manifest.json", manifest)
        (pack / "server.sql").write_text(n.server_sql(manifest), encoding="utf-8")
        n.load_pack(pack)
        index = len(server_spells)
        server_spells.append(spell)
        server_strings.update({(index, f): v for (_, f), v in strings.items()})
        learn.append((name, spell_id, item))
    server = args.stage / "server"
    server.mkdir()
    (server / "Spell.dbc1-native-retail-eclipse-hearths").write_bytes(n.make_wdbc(234, server_spells, server_strings))
    (server / "eclipse-hearths.sql").write_text("".join((p / "server.sql").read_text(encoding="utf-8") for p in sorted(packs.iterdir())),
                                                encoding="utf-8")
    source = ed.storm()
    handle = source.open_archive(HEARTH_ARCHIVE)
    names = [name for name in ed.list_files(source, handle) if not name.startswith("(")]
    source.dll.SFileCloseArchive(handle)
    (args.stage / "assets.txt").write_text("\n".join(names) + "\n", encoding="ascii")
    lines = ["# Eclipse hearthstones\n\n| Hearthstone | Spell | Item |\n|---|---|---|"]
    lines += [f"| {name} | `.learn {spell}` | `.additem {item}` |" for name, spell, item in learn]
    (args.stage / "eclipse-hearth-commands.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps({"hearths": learn}, indent=2))


def pack(args):
    names = (args.stage / "assets.txt").read_text(encoding="ascii").splitlines()
    ei.pack_assets(ed.storm(), [HEARTH_ARCHIVE], names, args.stage / ARCHIVE)
    print(f"{len(names)} files -> {args.stage / ARCHIVE}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("stage", "pack", "install-client", "install-server"))
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--client", type=Path, default=ed.CLIENT)
    parser.add_argument("--server-root", type=Path, default=ed.SERVER_TOOLS.parent)
    args = parser.parse_args()
    {"stage": stage, "pack": pack,
     "install-client": lambda a: ei.install_client(a.client, a.stage, ARCHIVE, "eclipse-hearths"),
     "install-server": lambda a: ei.install_server(a.stage, "eclipse-hearths.sql")}[args.command](args)
