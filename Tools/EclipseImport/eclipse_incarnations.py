"""Import Eclipse's incarnations (alternative druid-form, Metamorphosis and warlock-demon bodies) as per-character choices.

  stage   display rows (reused when the server already has the same model + textures), the world catalog
          (esteria_incarnation_category / _form / esteria_incarnation), the picker's data file, the asset list
  pack | install-client | install-server   (as eclipse_mounts.py; install-server also applies the catalog)

Ammunition looks are not imported: a projectile is drawn from the client's own ammo item, so it is not a body the
server can show to other players.
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
import eclipse_hearths as eh
import eclipse_install as ei
import native_import as n
from server import query, snapshot

ARCHIVE = "patch-EclipseIncarnations.MPQ"
SOURCE_ARCHIVE = ed.ECLIPSE / "packs/incarneclipse.mpq"
KINDS = {1: "form", 2: "summon"}


def catalogue():
    data = (ed.ECLIPSE / "data/incarnations/incarn.edb").read_bytes()
    magic, version, n_cat, n_entry, n_form, n_stock, string_bytes, _ = struct.unpack_from("<4s7I", data)
    if magic != b"EIDB" or version != 3:
        raise ValueError("incarn.edb is not an EIDB v3 catalogue")
    p = 32
    cats = [struct.unpack_from("<7I", data, p + i * 28) for i in range(n_cat)]
    p += n_cat * 28
    forms = [struct.unpack_from("<2I", data, p + i * 8) for i in range(n_form)]
    p += (n_form + n_stock) * 8
    entries = [struct.unpack_from("<10I", data, p + i * 40) for i in range(n_entry)]
    pool = data[p + n_entry * 40:]
    # Esteria is classless (every character is a Hero who can learn any form), so every slot is open to every class.
    categories = [{"id": c[0], "kind": c[1], "key": c[2], "class_mask": 0xFFFFFFFF, "name": ed.cstr(pool, c[4]),
                   "icon": ed.cstr(pool, c[5]), "slug": ed.cstr(pool, c[6])} for c in cats if c[1] in KINDS]
    wanted = {c["id"] for c in categories}
    looks = [{"display": e[0], "category": e[2], "name": ed.cstr(pool, e[4]), "model": ed.cstr(pool, e[5])}
             for e in entries if e[2] in wanted]
    return categories, [f for f in forms if f[1] in wanted], looks


def stage(args):
    packs = args.stage / "packs"
    if packs.exists():
        raise SystemExit(f"{packs} already exists; stage into a new directory")
    categories, forms, looks = catalogue()
    config = dict(line.strip().split("=", 1) for line in (args.client / "Data/Retail/RetailCasc.ini").read_text().splitlines() if "=" in line)
    product, build = config["product"], config["build"]
    cdi = ed.Edt(ed.ECLIPSE / "data/tables/CreatureDisplayInfo.edt")
    cmd = ed.Edt(ed.ECLIPSE / "data/tables/CreatureModelData.edt")
    client, providers = n.client_ids(args.client, args.server_root)  # opens every archive itself, so before Archives
    server_ids, server_providers = n.server_table_ids()
    archives = ed.Archives(args.client)
    client_cdi, client_cmd = archives.tables("CreatureDisplayInfo"), archives.tables("CreatureModelData")
    source = ed.storm()
    handle = source.open_archive(SOURCE_ARCHIVE)
    eclipse_names = {ed.norm(name): name for name in ed.list_files(source, handle)}
    server_sql_cdi = {int(v) for v in query("SELECT ID FROM creaturedisplayinfo_dbc;").splitlines()}
    model_info = set(snapshot(("creature_model_info",))["creature_model_info"]["ids"])
    # A display the server knows (and the client ships) with the same model and textures is reused as is.
    known = {}
    for display, (row, pool) in client_cdi.items():
        if (display in server_ids["CreatureDisplayInfo"] or display in server_sql_cdi) and row[1] in client_cmd:
            mrow, mpool = client_cmd[row[1]]
            known.setdefault((ed.norm(ed.as_m2(ed.cstr(mpool, mrow[2]))), tuple(ed.cstr(pool, row[i]).lower() for i in (6, 7, 8))), display)
    models_known = {ed.norm(ed.as_m2(ed.cstr(pool, row[2]))): mid for mid, (row, pool) in client_cmd.items()
                    if mid in server_ids["CreatureModelData"]}
    allocations = args.client / "Data/Retail/allocations.sqlite"
    used = {t: client[t] | server_ids[t] for t in ("CreatureDisplayInfo", "CreatureModelData")}
    used["CreatureDisplayInfo"] |= server_sql_cdi | model_info
    with closing(sqlite3.connect(allocations)) as db:
        for kind in used:
            used[kind] |= {r[0] for r in db.execute("SELECT id FROM mount_allocations WHERE kind=?", (kind,))}
    cdi_rows, cdi_strings, cmd_rows, cmd_strings, files, entries, model_ids = [], {}, [], {}, set(), [], {}
    reused = 0
    for look in looks:
        drow = cdi.rows[look["display"]]
        mrow = cmd.rows[drow[1]]
        model = ed.as_m2(cmd.text(mrow[2]))
        textures = [cdi.text(drow[i]) for i in (6, 7, 8)]
        key = (ed.norm(model), tuple(t.lower() for t in textures))
        if key in known:
            entries.append((look, known[key]))
            reused += 1
            continue
        model_key = ed.norm(model)
        data = source.read(handle, eclipse_names[model_key]) if model_key in eclipse_names else None
        if data is None and not archives.has(model):
            print("skip (no model):", look["name"], model)
            continue
        folder = model_key.rsplit(ed.BS, 1)[0]
        needed = {model_key} | {ed.norm(f"{folder}\\{t}.blp") for t in textures if t}
        if data:
            needed |= set(ed.m2_skins(model, data)) | {ed.norm(t) for t in ed.m2_textures(data)}
            needed |= {a for a in ed.m2_external_anims(model, data) if a in eclipse_names or archives.has(a)}
        missing = [f for f in needed if f not in eclipse_names and not archives.has(f)]
        if missing:
            print("skip (missing files):", look["name"], missing[:2])
            continue
        # Where Esteria owns the .m2, its skins/anims must come with it: Eclipse's would not match (felhound401.skin crash).
        own_model = model_key in eclipse_names and not archives.has(model)
        files |= {f for f in needed if f in eclipse_names and not archives.has(f)
                  and (own_model or not f.endswith((".skin", ".anim")))}
        key_base = f"{product}:{build}:eclipse-incarnation"
        if model_key in models_known:
            model_id = models_known[model_key]
        elif model_key in model_ids:
            model_id = model_ids[model_key]
        else:
            model_id = eh.allocate(allocations, f"{key_base}:model:{model_key}", "CreatureModelData", used["CreatureModelData"], 200000)
            row = mrow[:28]
            row[0] = model_id
            row[5] = row[6] = row[13] = 0  # realm blood, footprint and sound ids
            cmd_strings[(len(cmd_rows), 2)] = model
            cmd_rows.append(row)
        model_ids[model_key] = model_id
        display = eh.allocate(allocations, f"{key_base}:display:{look['display']}", "CreatureDisplayInfo", used["CreatureDisplayInfo"], 200000)
        row = drow[:16]
        row[0], row[1] = display, model_id
        row[2] = row[3] = row[11] = row[12] = row[13] = row[15] = 0  # realm sound/extra/blood/npc-sound/particle/effect ids
        row[6] = row[7] = row[8] = row[9] = 0  # string offsets into Eclipse's pool; textures re-added below
        for field, texture in zip((6, 7, 8), textures):
            if texture:
                cdi_strings[(len(cdi_rows), field)] = texture
        cdi_rows.append(row)
        entries.append((look, display))
    source.dll.SFileCloseArchive(handle)
    archives.close()
    packs.mkdir(parents=True)
    pack = packs / "eclipse-incarnations"
    (pack / "tables").mkdir(parents=True)
    (pack / "tables/creaturedisplayinfo.dbc").write_bytes(n.make_wdbc(16, cdi_rows, cdi_strings))
    (pack / "tables/creaturemodeldata.dbc").write_bytes(n.make_wdbc(28, cmd_rows, cmd_strings))
    (pack / "equipment.csv").write_text("", encoding="ascii")
    source_info = {"product": product, "build": build, "identity_kind": "eclipse-incarnations", "asset_bytes": "eclipse-wotlk-mpq"}
    target = {"kind": "client-rows", "name": "Eclipse incarnations", "records": {},
              "displays": {str(look["display"]): display for look, display in entries}}
    manifest = {"schema": 1, "slug": "eclipse-incarnations", "source_key": f"{product}:{build}:eclipse-incarnations",
                "source": source_info, "target": target,
                "baselines": {"client_providers": providers, "server_providers": server_providers}}
    manifest["definition_sha256"] = n.digest(json.dumps({"source": source_info, "target": target}, sort_keys=True).encode())
    manifest["files"] = {p.relative_to(pack).as_posix(): {"sha256": n.digest(p.read_bytes()), "size": p.stat().st_size}
                         for p in sorted(pack.rglob("*")) if p.is_file()}
    n.save_json(pack / "manifest.json", manifest)
    (pack / "server.sql").write_text("", encoding="utf-8")
    n.load_pack(pack)
    server = args.stage / "server"
    server.mkdir()
    (server / "CreatureDisplayInfo.dbc1-native-retail-eclipse-incarnations").write_bytes(n.make_wdbc(16, cdi_rows, cdi_strings))
    (server / "CreatureModelData.dbc1-native-retail-eclipse-incarnations").write_bytes(n.make_wdbc(28, cmd_rows, cmd_strings))
    q = lambda s: "'" + s.replace("\\", "\\\\").replace("'", "''") + "'"
    sql = [
        "-- Eclipse incarnations: the catalogue the server validates choices against. Re-runnable.",
        "CREATE TABLE IF NOT EXISTS `esteria_incarnation_category` (`id` int unsigned NOT NULL, `kind` tinyint unsigned NOT NULL,"
        " `key` int unsigned NOT NULL, `class_mask` int unsigned NOT NULL, `slug` varchar(32) NOT NULL, `name` varchar(64) NOT NULL,"
        " PRIMARY KEY (`id`)) ENGINE=InnoDB;",
        "CREATE TABLE IF NOT EXISTS `esteria_incarnation_form` (`form` int unsigned NOT NULL, `category` int unsigned NOT NULL,"
        " PRIMARY KEY (`form`)) ENGINE=InnoDB;",
        "CREATE TABLE IF NOT EXISTS `esteria_incarnation` (`category` int unsigned NOT NULL, `display` int unsigned NOT NULL,"
        " `name` varchar(100) NOT NULL, PRIMARY KEY (`category`, `display`)) ENGINE=InnoDB;",
        "START TRANSACTION;",
        "DELETE FROM `esteria_incarnation_category`; DELETE FROM `esteria_incarnation_form`; DELETE FROM `esteria_incarnation`;",
    ]
    sql += [f"INSERT INTO `esteria_incarnation_category` VALUES ({c['id']},{c['kind']},{c['key']},{c['class_mask']},{q(c['slug'])},{q(c['name'])});"
            for c in categories]
    sql += [f"INSERT INTO `esteria_incarnation_form` VALUES ({form},{category});" for form, category in forms]
    seen = set()
    for look, display in entries:
        if (look["category"], display) not in seen:
            seen.add((look["category"], display))
            sql.append(f"INSERT INTO `esteria_incarnation` VALUES ({look['category']},{display},{q(look['name'])});")
    new_displays = [row[0] for row in cdi_rows]
    sql += [f"INSERT IGNORE INTO `creature_model_info` (`DisplayID`,`BoundingRadius`,`CombatReach`,`Gender`,`DisplayID_Other_Gender`,`VerifiedBuild`)"
            f" VALUES ({d},1,1.5,2,0,12340);" for d in new_displays]
    sql.append("COMMIT;")
    (server / "eclipse-incarnations.sql").write_text("\n".join(sql) + "\n", encoding="utf-8")
    (args.stage / "assets.txt").write_text("\n".join(sorted(files)) + "\n", encoding="ascii")
    # The picker's data: categories in display order, each look's name and display.
    lua = ["-- Generated by Tools/EclipseImport/eclipse_incarnations.py; do not edit.", "EsteriaIncarnationsData = {"]
    for c in categories:
        looks_here = sorted({(look["name"], display) for look, display in entries if look["category"] == c["id"]})
        lua.append(f'  {{ id = {c["id"]}, kind = "{KINDS[c["kind"]]}", key = {c["key"]}, classMask = {c["class_mask"]}, '
                   f'name = {json.dumps(c["name"])}, looks = {{')
        lua += [f"    {{ {display}, {json.dumps(name)} }}," for name, display in looks_here]
        lua.append("  } },")
    lua.append("}")
    (args.stage / "EsteriaIncarnationsData.lua").write_text("\n".join(lua) + "\n", encoding="utf-8")
    print(json.dumps({"looks": len(entries), "reused_displays": reused, "new_displays": len(cdi_rows),
                      "new_models": len(cmd_rows), "files": len(files)}, indent=2))


def picker_data(args):
    """The picker's Lua data from what is installed: the server catalogue, each look's model path (client display
    tables + supplemental registry) and each slot's icon (incarn.edb)."""
    categories, _, _ = catalogue()
    icons = {c["id"]: c["icon"] for c in categories}
    rows = [line.split("\t") for line in query("SELECT c.id, c.kind, c.`key`, c.name, i.display, i.name FROM esteria_incarnation_category c "
                                               "JOIN esteria_incarnation i ON i.category = c.id ORDER BY c.id, i.name;").splitlines()]
    archives = ed.Archives(args.client)
    displays, models = archives.tables("CreatureDisplayInfo"), archives.tables("CreatureModelData")
    archives.close()
    config = dict(line.strip().split("=", 1) for line in (args.client / "Data/Retail/RetailCasc.ini").read_text().splitlines() if "=" in line)
    registry = args.client / "Data/Retail" / config["tables"]
    for table, target, fields in (("creaturedisplayinfo", displays, 16), ("creaturemodeldata", models, 28)):
        reg_rows, pool = n.wdbc((registry / f"{table}.dbc").read_bytes(), fields)
        target.update({row[0]: (row, pool) for row in reg_rows})
    def model_of(display):
        drow = displays.get(int(display))
        mrow = models.get(drow[0][1]) if drow else None
        return ed.as_m2(ed.cstr(mrow[1], mrow[0][2])) if mrow else None

    # The stock body of each slot, as the server picks it: player_shapeshift_model by race (Unit::GetModelForForm),
    # else the form's own Alliance/Horde display; a demon's creature display.
    archives = ed.Archives(args.client)
    forms, races = archives.tables("SpellShapeshiftForm"), archives.tables("ChrRaces")
    archives.close()
    # the column UnitRace's second value comes from: the one that reads "NightElf" on race 4
    elf_row, elf_pool = races[4]
    column = next(i for i, v in enumerate(elf_row) if 0 < v < len(elf_pool) and ed.cstr(elf_pool, v) == "NightElf")
    race_files = {ed.cstr(pool, row[column]): race_id for race_id, (row, pool) in races.items()
                  if 0 < row[column] < len(pool) and ed.cstr(pool, row[column]).isidentifier()}
    by_race = {}
    for form, race, display in (line.split("\t") for line in query(
            "SELECT ShapeshiftID, RaceID, ModelID FROM player_shapeshift_model ORDER BY CustomizationID, GenderID;").splitlines()):
        by_race.setdefault(int(form), {}).setdefault(int(race), int(display))
    creature_display = dict((int(c), int(d)) for c, d in (line.split("\t") for line in query(
        "SELECT CreatureID, CreatureDisplayID FROM creature_template_model WHERE Idx=0;").splitlines()))

    def default_of(kind, key):
        def entry(display):
            path = model_of(display) if display else None
            return f"{{ {display}, {json.dumps(path)} }}" if path else "nil"
        if KINDS[int(kind)] == "summon":
            return f"{{ any = {entry(creature_display.get(int(key)))} }}"
        row = forms.get(int(key), ([0] * 35, b""))[0]
        race_entries = ", ".join(f"[{race}] = {entry(d)}" for race, d in sorted(by_race.get(int(key), {}).items()) if model_of(d))
        return f"{{ a = {entry(row[23])}, h = {entry(row[24] or row[23])}, races = {{ {race_entries} }} }}"

    lua = ["-- Generated by Tools/EclipseImport/eclipse_incarnations.py data; do not edit.",
           "-- { id, kind, key, name, icon, default, looks = { { display, name, model }, ... } }",
           "EsteriaIncarnationsRaces = { " + ", ".join(f"{k} = {v}" for k, v in sorted(race_files.items())) + " }",
           "EsteriaIncarnationsData = {"]
    current, missing = None, 0
    for cid, kind, key, cname, display, name in rows:
        if cid != current:
            if current is not None:
                lua.append("  } },")
            current = cid
            lua.append(f'  {{ id = {cid}, kind = "{KINDS[int(kind)]}", key = {key}, name = {json.dumps(cname)}, '
                       f'icon = {json.dumps("Interface\\Icons\\" + icons.get(int(cid), "INV_Misc_QuestionMark"))}, '
                       f'default = {default_of(kind, key)}, looks = {{')
        drow = displays.get(int(display))
        mrow = models.get(drow[0][1]) if drow else None
        if not mrow:
            missing += 1
            continue
        model = ed.as_m2(ed.cstr(mrow[1], mrow[0][2]))
        lua.append(f"    {{ {display}, {json.dumps(name)}, {json.dumps(model)} }},")
    lua += ["  } },", "}"]
    out = Path(__file__).resolve().parents[2] / "Addons/EsteriaIncarnations/EsteriaIncarnationsData.lua"
    out.write_text("\n".join(lua) + "\n", encoding="utf-8")
    print(f"{len(rows) - missing} looks written to {out}" + (f"; {missing} without a display row" if missing else ""))


def pack(args):
    names = (args.stage / "assets.txt").read_text(encoding="ascii").splitlines()
    ei.pack_assets(ed.storm(), [SOURCE_ARCHIVE], [n_ for n_ in names if n_], args.stage / ARCHIVE)
    print(f"{len(names)} files -> {args.stage / ARCHIVE}")


def install_server(args):
    ei.install_server(args.stage, "eclipse-incarnations.sql")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("stage", "pack", "install-client", "install-server", "data"))
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--client", type=Path, default=ed.CLIENT)
    parser.add_argument("--server-root", type=Path, default=ed.SERVER_TOOLS.parent)
    args = parser.parse_args()
    {"stage": stage, "pack": pack, "install-server": install_server, "data": picker_data,
     "install-client": lambda a: ei.install_client(a.client, a.stage, ARCHIVE, "eclipse-incarnations")}[args.command](args)
