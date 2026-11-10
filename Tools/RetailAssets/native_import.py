"""Import original Retail weapon assets as additive Esteria client/server content.

This first backend takes resolved model/texture IDs. Retail item/set DB2 discovery,
armor composition and mount integration are separate milestones.
"""
from __future__ import annotations

import argparse
from contextlib import closing
import hashlib
import importlib.util
import json
import os
import re
import shutil
import sqlite3
import struct
import subprocess
import sys
from pathlib import Path

from server import query, snapshot, KEYS

TABLES = {"Item": (8, ()), "ItemDisplayInfo": (25, (*range(1, 7), *range(15, 23)))}
MOUNT_TABLES = {"CreatureModelData": (28, (2,)), "CreatureDisplayInfo": (16, (6, 7, 8, 9)),
                "Spell": (234, (*range(136, 152), *range(153, 169), *range(170, 186), *range(187, 203))),
                "SpellIcon": (2, (1,)), "SkillLineAbility": (14, ())}
TABLES.update(MOUNT_TABLES)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def save_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def wdbc(data, fields):
    if len(data) < 21:
        raise ValueError("Truncated WDBC")
    magic, count, actual, stride, strings = struct.unpack_from("<4s4I", data)
    if magic != b"WDBC" or actual != fields or stride != fields * 4 or not strings:
        raise ValueError("Unexpected WDBC layout")
    start = 20 + count * stride
    if start + strings != len(data) or data[start] or data[-1]:
        raise ValueError("Invalid WDBC bounds/string pool")
    rows = [list(struct.unpack_from(f"<{fields}I", data, 20 + i * stride)) for i in range(count)]
    if len({row[0] for row in rows}) != count:
        raise ValueError("Duplicate WDBC IDs")
    return rows, data[start:]


def make_wdbc(fields, rows, strings=None):
    rows = [row.copy() for row in rows]
    pool = bytearray(b"\0")
    for (index, field), value in (strings or {}).items():
        rows[index][field] = len(pool)
        pool.extend(value.encode("utf-8") + b"\0")
    return (struct.pack("<4s4I", b"WDBC", len(rows), fields, fields * 4, len(pool)) +
            b"".join(struct.pack(f"<{fields}I", *(value & 0xFFFFFFFF for value in row)) for row in rows) + pool)


def client_ids(client, server_root, exclude_pack=None):
    """Conservatively reserve IDs from EVERY mounted-location provider, not just a guessed winner."""
    module_path = server_root / "tools/cars_mount_pack.py"
    spec = importlib.util.spec_from_file_location("esteria_mpq_inspection", module_path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    dll = Path(os.environ.get("ESTERIA_STORMLIB", module.DLL_DEFAULT))
    if not dll.is_file():
        raise ValueError("Esteria's configured StormLib DLL is missing; set ESTERIA_STORMLIB")
    storm = module.Storm(dll)
    data = client / "Data"
    roots = [data] + [p for p in data.iterdir() if p.is_dir() and re.fullmatch(r"[a-z]{2}[A-Z]{2}", p.name)]
    ids = {table: set() for table in TABLES}
    providers = []
    for root in roots:
        # Folder MPQs and loose continuation providers also reserve IDs.
        for loose in sorted(root.rglob("*.dbc*")):
            if "Retail" in loose.relative_to(data).parts or not loose.is_file(): continue
            table = next((name for name in TABLES if loose.name.casefold().startswith(name.casefold() + ".dbc")), None)
            if table is None: continue
            payload = loose.read_bytes()
            rows, _ = wdbc(payload, struct.unpack_from("<I", payload, 8)[0])
            ids[table].update(row[0] for row in rows)
            providers.append({"file": str(loose), "table": table, "sha256": digest(payload)})
        for archive in sorted(root.iterdir()):
            if archive.suffix.lower() != ".mpq" or not archive.is_file():
                continue
            handle = storm.open_archive(archive)
            try:
                for table, (fields, _) in TABLES.items():
                    key = f"DBFilesClient\\{table}.dbc"
                    # Existence and read failures are distinct; never ignore a corrupt provider.
                    import ctypes
                    file_handle = module.H()
                    if not storm.dll.SFileOpenFileEx(handle, key.encode("ascii"), 0, ctypes.byref(file_handle)):
                        if ctypes.get_last_error() in (2, 3):
                            continue
                        raise OSError(f"Cannot inspect {key} in {archive}: {ctypes.get_last_error()}")
                    storm.dll.SFileCloseFile(file_handle)
                    payload = storm.read(handle, key)
                    try:
                        # Older shadowed Item.dbc providers have four columns. Their
                        # IDs must still be reserved; only new deltas require eight.
                        provider_fields = struct.unpack_from("<I", payload, 8)[0]
                        if not 1 <= provider_fields <= 512:
                            raise ValueError("Invalid provider column count")
                        rows, _ = wdbc(payload, provider_fields)
                    except ValueError as exc:
                        raise ValueError(f"{archive}: {table}: {struct.unpack_from('<4s4I', payload)}: {exc}") from exc
                    ids[table].update(row[0] for row in rows)
                    providers.append({"archive": str(archive), "table": table, "sha256": digest(payload)})
            finally:
                storm.dll.SFileCloseArchive(handle)
        for table, (fields, _) in TABLES.items():
            for loose in (root / "DBFilesClient" / f"{table}.dbc", data / "Retail/Registry" / f"{table.lower()}.dbc"):
                if loose.is_file():
                    payload = loose.read_bytes()
                    ids[table].update(row[0] for row in wdbc(payload, fields)[0])
                    providers.append({"file": str(loose), "table": table, "sha256": digest(payload)})
    for table, (fields, _) in TABLES.items():
        for path in sorted((data / "Retail/Packs").glob(f"*/tables/{table.lower()}.dbc")):
            if path.parent.parent.name == exclude_pack:
                continue
            payload = path.read_bytes()
            ids[table].update(row[0] for row in wdbc(payload, fields)[0])
    if any(not value for value in ids.values()):
        raise ValueError("Could not establish client table ID baselines")
    return ids, providers


def allocate(path, source_key, item_used, display_used):
    path.parent.mkdir(parents=True, exist_ok=True)
    with closing(sqlite3.connect(path, timeout=30)) as db, db:
        db.execute("CREATE TABLE IF NOT EXISTS allocations (source_key TEXT PRIMARY KEY, item_id INTEGER UNIQUE, display_id INTEGER UNIQUE)")
        db.execute("BEGIN IMMEDIATE")
        previous = db.execute("SELECT item_id,display_id FROM allocations WHERE source_key=?", (source_key,)).fetchone()
        if previous:
            return previous
        for item, display in db.execute("SELECT item_id,display_id FROM allocations"):
            item_used.add(item)
            display_used.add(display)
        def free(used, start):
            while start in used:
                start += 1
            if start > 5000000:
                raise ValueError("No target ID within the native table index budget")
            return start
        result = free(item_used, 1000000), free(display_used, 160000)
        db.execute("INSERT INTO allocations VALUES (?,?,?)", (source_key, *result))
        return result


def server_table_ids():
    result = {table: set() for table in TABLES}
    paths = subprocess.run(["docker", "exec", "ac-worldserver", "find", "/azerothcore/env/dist/data",
                            "-maxdepth", "3", "-type", "f", "-iname", "*.dbc*"],
                           capture_output=True, text=True, check=True).stdout.splitlines()
    providers = []
    for path in paths:
        table = next((name for name in TABLES if Path(path).name.casefold().startswith(name.casefold() + ".dbc")), None)
        if not table:
            continue
        data = subprocess.run(["docker", "exec", "ac-worldserver", "cat", path], capture_output=True, check=True).stdout
        fields = struct.unpack_from("<I", data, 8)[0]
        if fields not in (4, TABLES[table][0]):
            raise ValueError("Unexpected active server table layout")
        result[table].update(row[0] for row in wdbc(data, fields)[0])
        providers.append({"path": path, "sha256": digest(data)})
    if any(not ids for ids in result.values()):
        raise ValueError("Both active server item/display table baselines are required")
    return result, providers


def literal(value):
    if isinstance(value, int):
        return str(value)
    return "CONVERT(0x" + str(value).encode("utf-8").hex() + " USING utf8mb4) COLLATE utf8mb4_bin"


def server_sql(manifest):
    target = manifest["target"]
    key, fingerprint = manifest["source_key"], manifest["definition_sha256"]
    records = target["records"]
    lines = [
        "-- Additive native Retail import. Conflicts abort; existing content is never replaced.",
        "CREATE TABLE IF NOT EXISTS `esteria_retail_imports` (",
        "  `source_key` varchar(160) NOT NULL, `definition_sha256` char(64) NOT NULL,",
        "  `item_entry` int unsigned NOT NULL, `display_id` int unsigned NOT NULL,",
        "  PRIMARY KEY (`source_key`), UNIQUE (`item_entry`), UNIQUE (`display_id`)",
        ") ENGINE=InnoDB;",
        "CREATE TEMPORARY TABLE `_native_retail_guard` (`ok` int NOT NULL CHECK (`ok` = 1));",
        "INSERT INTO `_native_retail_guard` SELECT GET_LOCK('esteria_native_retail_import', 10);",
        "START TRANSACTION;",
        "INSERT INTO `_native_retail_guard` SELECT 0 FROM `esteria_retail_imports` WHERE "
        f"`source_key`={literal(key)} AND (`definition_sha256`<>{literal(fingerprint)} OR "
        f"`item_entry`<>{target['item_id']} OR `display_id`<>{target['display_id']});",
    ]
    owned = f"EXISTS (SELECT 1 FROM `esteria_retail_imports` WHERE `source_key`={literal(key)} AND `definition_sha256`={literal(fingerprint)})"
    for table, record in sorted(records.items()):
        record = dict(sorted(record.items()))
        primary = KEYS[table]
        same = " AND ".join(f"`{column}` <=> {literal(value)}" for column, value in record.items())
        lines.append(f"INSERT INTO `_native_retail_guard` SELECT 0 FROM `{table}` WHERE `{primary}`={record[primary]} AND (NOT ({owned}) OR NOT ({same}));")
        columns = ",".join(f"`{column}`" for column in record)
        values = ",".join(literal(value) for value in record.values())
        lines.append(f"INSERT INTO `{table}` ({columns}) SELECT {values} WHERE NOT EXISTS (SELECT 1 FROM `{table}` WHERE `{primary}`={record[primary]});")
    lines += [
        "INSERT INTO `esteria_retail_imports` (`source_key`,`definition_sha256`,`item_entry`,`display_id`) "
        f"SELECT {literal(key)},{literal(fingerprint)},{target['item_id']},{target['display_id']} WHERE NOT ({owned});",
        "COMMIT;", "DO RELEASE_LOCK('esteria_native_retail_import');", "DROP TEMPORARY TABLE `_native_retail_guard`;",
    ]
    return "\n".join(lines) + "\n"


def stage_weapon(args):
    if not re.fullmatch(r"[a-z][a-z0-9-]{0,63}", args.slug):
        raise ValueError("Pack slug must be lowercase letters, numbers and hyphens")
    if args.stage.exists():
        raise ValueError("Stage must be a new directory; use a new stage for a repeat import")
    if not 1 <= args.model <= 0xFFFFFFFF or not 1 <= args.texture <= 0xFFFFFFFF:
        raise ValueError("Positive model/texture FileDataIDs are required")
    config = dict(line.split("=", 1) for line in args.source_config.read_text().splitlines()
                  if "=" in line and not line.lstrip().startswith(("#", ";")))
    build, product = config.get("build", "").strip(), config.get("product", "wow").strip()
    if not re.fullmatch(r"[0-9a-f]{32}", build) or product not in {"wow", "wowt", "wow_beta"}:
        raise ValueError("Source acquisition requires a pinned product/build")
    source_key = f"{product}:{build}:weapon:{args.model}:{args.texture}"
    client, providers = client_ids(args.client, args.server_root)
    server = snapshot()
    server_ids, server_providers = server_table_ids()
    args.stage.mkdir(parents=True)
    subprocess.run([str(args.acquire.resolve()), str(args.source_config.resolve()), str(args.model), str(args.texture),
                    str((args.stage / "originals").resolve())], check=True)
    item, display = allocate(args.client / "Data/Retail/allocations.sqlite", source_key,
                             client["Item"] | server_ids["Item"] | set(server["item_template"]["ids"]) | set(server["item_dbc"]["ids"]),
                             client["ItemDisplayInfo"] | server_ids["ItemDisplayInfo"] | set(server["itemdisplayinfo_dbc"]["ids"]))
    item_row = [item, 2, 7, 0xFFFFFFFF, 1, display, 13, 3]
    display_row = [display] + [0] * 24
    display_strings = {(0, 1): f"retail/{args.model}.m2", (0, 3): f"retail/{args.texture}", (0, 5): "INV_Sword_04"}
    (args.stage / "tables").mkdir()
    (args.stage / "tables/item.dbc").write_bytes(make_wdbc(8, [item_row]))
    (args.stage / "tables/itemdisplayinfo.dbc").write_bytes(make_wdbc(25, [display_row], display_strings))
    records = {
        "item_template": {"entry": item, "class": 2, "subclass": 7, "SoundOverrideSubclass": -1,
                          "name": args.name, "displayid": display, "Quality": 3, "InventoryType": 13,
                          "ItemLevel": 1, "RequiredLevel": 1, "dmg_min1": 1, "dmg_max1": 2, "delay": 2000,
                          "Material": 1, "sheath": 3, "stackable": 1,
                          "description": "Original Retail CDN assets. Native import proof; Esteria test gameplay."},
        "item_dbc": dict(zip(("ID", "ClassID", "SubclassID", "Sound_Override_Subclassid", "Material", "DisplayInfoID", "InventoryType", "SheatheType"),
                             [item, 2, 7, -1, 1, display, 13, 3])),
        "itemdisplayinfo_dbc": {"ID": display, "ModelName_1": f"retail/{args.model}.m2", "ModelTexture_1": f"retail/{args.texture}", "InventoryIcon_1": "INV_Sword_04"},
    }
    for table, record in records.items():
        columns = {row.split("\t")[0] for row in server[table]["columns"]}
        if not set(record) <= columns:
            raise ValueError(f"Unsupported actual server schema for {table}")
    manifest = {
        "schema": 1, "slug": args.slug, "source_key": source_key,
        "source": {"product": product, "build": build, "model_file_id": args.model, "texture_file_id": args.texture,
                   "retail_item_id": None, "identity_kind": "resolved-model-texture", "asset_bytes": "original-decoded-CASC"},
        "target": {"item_id": item, "display_id": display, "name": args.name, "records": records},
        "limitations": ["Explicit resolved file IDs; automatic Retail item/set metadata discovery is not implemented.",
                        "Authored level-one test weapon stats; not Retail gameplay scaling.",
                        "Legacy fallback inventory icon; original source icon not yet resolved.",
                        "Existing validated in-memory model adapter; no converted asset files are emitted.",
                        "One pinned source build per installed client registry."],
        "baselines": {"client_providers": providers, "server_providers": server_providers,
                      "server_schema": {name: data["columns"] for name, data in server.items()}},
    }
    manifest["definition_sha256"] = digest(json.dumps({"source": manifest["source"], "target": manifest["target"]}, sort_keys=True).encode())
    (args.stage / "equipment.csv").write_text(f"{display};{args.model};{args.texture}\n", encoding="ascii")
    files = sorted(p for p in args.stage.rglob("*") if p.is_file())
    manifest["files"] = {p.relative_to(args.stage).as_posix(): {"sha256": digest(p.read_bytes()), "size": p.stat().st_size} for p in files}
    save_json(args.stage / "manifest.json", manifest)
    (args.stage / "server.sql").write_text(server_sql(manifest), encoding="utf-8")
    print(json.dumps({"stage": str(args.stage.resolve()), "item_id": item, "display_id": display,
                      "original_files": len(list((args.stage / "originals").glob("*.bin")))}, indent=2))


def load_pack(stage):
    manifest = json.loads((stage / "manifest.json").read_text(encoding="utf-8"))
    source, target = manifest["source"], manifest["target"]
    if manifest["schema"] != 1 or not re.fullmatch(r"[a-z][a-z0-9-]{0,63}", manifest["slug"]):
        raise ValueError("Invalid pack schema or slug")
    if not re.fullmatch(r"[0-9a-f]{32}", source["build"]) or source["product"] not in {"wow", "wowt", "wow_beta"}:
        raise ValueError("Invalid source build")
    expected = {"item_template", "item_dbc", "itemdisplayinfo_dbc"}
    if target.get("kind") == "mount": expected |= {"creature_template", "creature_template_model", "creature_model_info"}
    if set(target["records"]) != expected:
        raise ValueError("Unexpected server record tables")
    if manifest["definition_sha256"] != digest(json.dumps({"source": source, "target": target}, sort_keys=True).encode()):
        raise ValueError("Pack definition fingerprint differs")
    for relative, expected in manifest["files"].items():
        path = stage / relative
        if path.resolve().is_relative_to(stage.resolve()) is False or path.is_symlink():
            raise ValueError("Invalid pack path")
        payload = path.read_bytes()
        if digest(payload) != expected["sha256"] or len(payload) != expected["size"]:
            raise ValueError(f"Pack file changed: {relative}")
    if (stage / "server.sql").read_text(encoding="utf-8") != server_sql(manifest):
        raise ValueError("Server migration differs from manifest")
    return manifest


def verify_server(manifest):
    for table, record in manifest["target"]["records"].items():
        where = " AND ".join(f"`{column}` <=> {literal(value)}" for column, value in record.items())
        if query(f"SELECT COUNT(*) FROM `{table}` WHERE {where};") != "1":
            raise ValueError(f"Installed server record does not match: {table}")
    owner = query("SELECT COUNT(*) FROM `esteria_retail_imports` WHERE "
                  f"`source_key`={literal(manifest['source_key'])} AND `definition_sha256`={literal(manifest['definition_sha256'])} "
                  f"AND `item_entry`={manifest['target']['item_id']} AND `display_id`={manifest['target']['display_id']};")
    if owner != "1":
        raise ValueError("Server import ownership receipt is missing")


def combine_tables(packs, name):
    fields, string_fields = TABLES[name]
    result, values, ids = [], {}, set()
    for pack in packs:
        path = pack / "tables" / f"{name.lower()}.dbc"
        if not path.is_file(): continue # Earlier weapon packs contain only equipment metadata.
        rows, pool = wdbc(path.read_bytes(), fields)
        for row in rows:
            if row[0] in ids:
                raise ValueError("Installed packs have conflicting client record IDs")
            ids.add(row[0])
            index = len(result)
            result.append(row)
            for field in string_fields:
                offset = row[field]
                if offset >= len(pool) or b"\0" not in pool[offset:]:
                    raise ValueError("Invalid pack string offset")
                if offset:
                    values[index, field] = pool[offset:pool.index(0, offset)].decode("utf-8")
    return make_wdbc(fields, result, values)


def install(args):
    manifest = load_pack(args.stage)
    root = args.client / "Data/Retail"
    state_file = root / "installed.json"
    state = json.loads(state_file.read_text()) if state_file.exists() else {"schema": 1, "packs": {}}
    slug = manifest["slug"]
    if not re.fullmatch(r"[a-z][a-z0-9-]{0,63}", slug):
        raise ValueError("Invalid pack slug")
    previous = state["packs"].get(slug)
    if previous and previous != {"source_key": manifest["source_key"], "definition_sha256": manifest["definition_sha256"]}:
        raise ValueError("Pack identity changed; explicit revision support is required")
    for installed_slug in state["packs"]:
        other = load_pack(root / "Packs" / installed_slug)
        if other["source"]["build"] != manifest["source"]["build"] or other["source"]["product"] != manifest["source"]["product"]:
            raise ValueError("This runtime registry supports one pinned source build")
        if other["source_key"] == manifest["source_key"] and installed_slug != slug:
            raise ValueError("Source already installed under another pack name")
    current = snapshot(manifest["baselines"]["server_schema"])
    if {name: data["columns"] for name, data in current.items()} != manifest["baselines"]["server_schema"]:
        raise ValueError("Server schema changed since staging")
    for table, record in manifest["target"]["records"].items():
        if not set(record) <= {row.split("\t")[0] for row in current[table]["columns"]}:
            raise ValueError("Unknown server record columns")
    # Recheck immutable provider contents, including ones that were shadowed.
    for provider in manifest["baselines"]["client_providers"]:
        if "file" in provider and "Retail" not in Path(provider["file"]).parts:
            if digest(Path(provider["file"]).read_bytes()) != provider["sha256"]:
                raise ValueError("Client baseline changed since staging")
    pack_path = root / "Packs" / slug
    if pack_path.exists() and load_pack(pack_path)["definition_sha256"] != manifest["definition_sha256"]:
        raise ValueError("Pack directory already contains different content")
    current_client_ids, current_providers = client_ids(args.client, args.server_root, exclude_pack=slug)
    staged_archives = {(p["archive"], p["table"]): p["sha256"] for p in manifest["baselines"]["client_providers"] if "archive" in p}
    live_archives = {(p["archive"], p["table"]): p["sha256"] for p in current_providers if "archive" in p}
    if staged_archives != live_archives:
        raise ValueError("Client archive table baseline changed since staging")
    current_server_ids, _ = server_table_ids()
    for table, (fields, _) in TABLES.items():
        path = args.stage / "tables" / f"{table.lower()}.dbc"
        if not path.exists(): continue
        new_ids = {row[0] for row in wdbc(path.read_bytes(), fields)[0]}
        used = current_client_ids[table] | current_server_ids[table]
        # On a repeat, the installed server continuation is this exact pack.
        if previous and manifest["target"].get("kind") == "mount": used -= new_ids
        if new_ids & used: raise ValueError(f"Target {table} ID collision after staging")
    if previous:
        installed = load_pack(pack_path)
        if installed["definition_sha256"] != manifest["definition_sha256"]:
            raise ValueError("Installed pack differs")
        assets = root / "Assets" / f"{manifest['source']['product']}-{manifest['source']['build']}"
        for original in (pack_path / "originals").glob("*.bin"):
            if digest((assets / original.name).read_bytes()) != digest(original.read_bytes()):
                raise ValueError("Installed original asset differs")
        verify_server(manifest)
        print(json.dumps({"status": "already-installed", "item_id": manifest["target"]["item_id"], "display_id": manifest["target"]["display_id"]}, indent=2))
        return
    if pack_path.exists():
        existing = load_pack(pack_path)
        if existing["definition_sha256"] != manifest["definition_sha256"]:
            raise ValueError("Pack directory already contains different content")
    else:
        pack_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(args.stage, pack_path)
    assets = root / "Assets" / f"{manifest['source']['product']}-{manifest['source']['build']}"
    assets.mkdir(parents=True, exist_ok=True)
    for original in (pack_path / "originals").glob("*.bin"):
        destination = assets / original.name
        if destination.exists() and digest(destination.read_bytes()) != digest(original.read_bytes()):
            raise ValueError("Shared original asset content differs for the pinned build")
        if not destination.exists():
            shutil.copy2(original, destination)
    state["packs"][slug] = {"source_key": manifest["source_key"], "definition_sha256": manifest["definition_sha256"]}
    packs = [root / "Packs" / name for name in sorted(state["packs"])]
    revision = digest(json.dumps(state, sort_keys=True).encode())[:16]
    registry = root / "Registries" / revision
    registry.mkdir(parents=True, exist_ok=True)
    for name in TABLES:
        data = combine_tables(packs, name)
        path = registry / f"{name.lower()}.dbc"
        if path.exists() and path.read_bytes() != data:
            raise ValueError("Immutable registry revision differs")
        path.write_bytes(data)
    equipment = "".join((pack / "equipment.csv").read_text(encoding="ascii") for pack in packs)
    (registry / "equipment.csv").write_text(equipment, encoding="ascii")
    aliases = "".join((pack / "aliases.csv").read_text(encoding="ascii") for pack in packs if (pack / "aliases.csv").is_file())
    (registry / "aliases.csv").write_text(aliases, encoding="ascii")
    # SQL guards serialize deployments and roll back every content row on a collision.
    query(server_sql(manifest))
    verify_server(manifest)
    if manifest["target"].get("kind") == "mount":
        from native_mount import publish_server_tables
        publish_server_tables(pack_path, manifest)
    config = (f"mode=files\npath=Assets/{assets.name}\nproduct={manifest['source']['product']}\n"
              f"build={manifest['source']['build']}\nlocale=2\ntables=Registries/{revision}\n"
              f"equipment=Registries/{revision}/equipment.csv\naliases=Registries/{revision}/aliases.csv\n")
    temporary = root / "RetailCasc.ini.tmp"
    temporary.write_text(config, encoding="ascii")
    temporary.replace(root / "RetailCasc.ini")
    save_json(state_file, state)
    receipt = {"state": "installed-awaiting-runtime-reload", "source_key": manifest["source_key"],
               "definition_sha256": manifest["definition_sha256"], "item_id": manifest["target"]["item_id"],
               "display_id": manifest["target"]["display_id"], "registry": revision,
               "files": manifest["files"], "server_readback": True}
    save_json(pack_path / "receipt.json", receipt)
    print(json.dumps({"installed": str(pack_path), "config": str(root / "RetailCasc.ini"),
                      "item_id": receipt["item_id"], "display_id": receipt["display_id"],
                      "status": receipt["state"]}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    stage = commands.add_parser("stage-weapon")
    for name in ("client", "server-root", "source-config", "acquire", "stage"):
        stage.add_argument("--" + name, type=Path, required=True)
    stage.add_argument("--slug", required=True)
    stage.add_argument("--name", required=True)
    stage.add_argument("--model", type=int, required=True)
    stage.add_argument("--texture", type=int, required=True)
    deploy = commands.add_parser("install")
    for name in ("client", "server-root", "stage"):
        deploy.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    if args.command == "stage-weapon":
        stage_weapon(args)
    elif args.command == "install":
        install(args)


if __name__ == "__main__":
    main()
