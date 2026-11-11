"""Rename installed Eclipse mounts everywhere (stage packs, client packs + registry, server rows + continuation).

    python rename_mounts.py --names mount_names_en.json [--client "G:/3.3.5a - Dev" --client "G:/3.3.5a - Live"]

Only names change, so IDs, models and allocations stay. The server's spell continuation is read at startup; item and
creature names are live after a client cache clear (Cache/WDB).
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import time
from pathlib import Path

import eclipse_install as ei
import native_import as n
import native_mount as nm
from server import query

STAGE = Path("G:/EclipseImport/mounts")
SPELL_STRINGS = n.TABLES["Spell"][1]


def table_strings(rows, pool, fields):
    return {(i, f): pool[r[f]:pool.index(0, r[f])].decode("utf-8") for i, r in enumerate(rows) for f in fields if r[f]}


def rename_pack(pack: Path, name: str):
    rows, pool = n.wdbc((pack / "tables/spell.dbc").read_bytes(), 234)
    strings = table_strings(rows, pool, SPELL_STRINGS)
    strings[(0, 136)] = strings[(0, 137)] = name
    (pack / "tables/spell.dbc").write_bytes(n.make_wdbc(234, rows, strings))
    source = json.loads((pack / "resolved-source.json").read_text())
    source["name"] = name
    n.save_json(pack / "resolved-source.json", source)
    manifest = json.loads((pack / "manifest.json").read_text())
    manifest["source"]["name"] = name
    target = manifest["target"]
    target["name"] = name
    target["records"]["item_template"]["name"] = "Reins of the " + name
    target["records"]["item_template"]["description"] = "Teaches you how to summon the " + name + "."
    target["records"]["creature_template"]["name"] = name
    manifest["definition_sha256"] = n.digest(json.dumps({"source": manifest["source"], "target": target}, sort_keys=True).encode())
    manifest["files"] = {p.relative_to(pack).as_posix(): {"sha256": n.digest(p.read_bytes()), "size": p.stat().st_size}
                         for p in sorted(pack.rglob("*")) if p.is_file() and p.name not in ("manifest.json", "server.sql")}
    n.save_json(pack / "manifest.json", manifest)
    (pack / "server.sql").write_text(n.server_sql(manifest), encoding="utf-8")
    return n.load_pack(pack)


def update_client(client: Path, changed: dict[str, dict], spell_names: dict[int, str]):
    root = client / "Data/Retail"
    backup = client / "_backups" / f"eclipse-mount-names-{time.strftime('%Y%m%d-%H%M%S')}"
    backup.mkdir(parents=True)
    for name in ("installed.json", "RetailCasc.ini"):
        shutil.copy2(root / name, backup / name)
    state = json.loads((root / "installed.json").read_text())
    for slug, manifest in changed.items():
        shutil.rmtree(root / "Packs" / slug)
        shutil.copytree(STAGE / "packs" / slug, root / "Packs" / slug)
        n.load_pack(root / "Packs" / slug)
        state["packs"][slug] = {"source_key": manifest["source_key"], "definition_sha256": manifest["definition_sha256"]}
    config = dict(line.strip().split("=", 1) for line in (root / "RetailCasc.ini").read_text().splitlines() if "=" in line)
    revision = n.digest((config["tables"] + "rename" + json.dumps(sorted(spell_names.items()))).encode())[:16]
    registry = root / "Registries" / revision
    staging = registry.with_name(revision + ".tmp")
    shutil.rmtree(staging, ignore_errors=True)
    shutil.copytree(root / config["tables"], staging)
    rows, pool = n.wdbc((staging / "spell.dbc").read_bytes(), 234)
    strings = table_strings(rows, pool, SPELL_STRINGS)
    hits = 0
    for i, row in enumerate(rows):
        if row[0] in spell_names:
            strings[(i, 136)] = strings[(i, 137)] = spell_names[row[0]]
            hits += 1
    if hits != len(spell_names):
        raise ValueError(f"{client}: registry holds {hits} of {len(spell_names)} renamed spells")
    (staging / "spell.dbc").write_bytes(n.make_wdbc(234, rows, strings))
    staging.rename(registry)
    lines = [f"tables=Registries/{revision}" if l.startswith("tables=") else
             f"aliases=Registries/{revision}/aliases.csv" if l.strip() == f"aliases={config['tables']}/aliases.csv" else l
             for l in (root / "RetailCasc.ini").read_text().splitlines()]
    (root / "RetailCasc.ini.tmp").write_text("\n".join(lines) + "\n", encoding="ascii")
    (root / "RetailCasc.ini.tmp").replace(root / "RetailCasc.ini")
    n.save_json(root / "installed.json", state)
    print(f"{client}: registry {revision}, {hits} spells renamed, backup {backup}")


def update_server(changed: dict[str, dict]):
    lit = n.literal
    sql = ["START TRANSACTION;"]
    for manifest in changed.values():
        records = manifest["target"]["records"]
        item, creature = records["item_template"], records["creature_template"]
        sql.append(f"UPDATE `item_template` SET `name`={lit(item['name'])}, `description`={lit(item['description'])} WHERE `entry`={item['entry']};")
        sql.append(f"UPDATE `creature_template` SET `name`={lit(creature['name'])} WHERE `entry`={creature['entry']};")
        sql.append(f"UPDATE `esteria_retail_imports` SET `definition_sha256`={lit(manifest['definition_sha256'])} "
                   f"WHERE `source_key`={lit(manifest['source_key'])};")
    sql.append("COMMIT;")
    query("\n".join(sql), timeout=600)
    for manifest in changed.values():
        n.verify_server(manifest)
    # The batch continuation carries every Eclipse mount spell: rebuild it from the packs and replace the file.
    rows, strings = [], {}
    for pack in sorted((STAGE / "packs").iterdir()):
        prow, ppool = n.wdbc((pack / "tables/spell.dbc").read_bytes(), 234)
        strings.update({(len(rows), f): v for (_, f), v in table_strings(prow, ppool, SPELL_STRINGS).items()})
        rows.append(prow[0])
    local = STAGE / "server" / "Spell.dbc1-native-retail-eclipse-mounts"
    local.write_bytes(n.make_wdbc(234, rows, strings))
    destination = f"{nm.SERVER_DATA}/dbc-continuations/{local.name}"
    subprocess.run(["docker", "cp", str(local.resolve()), f"ac-worldserver:{destination}"], check=True, capture_output=True)
    if nm.docker_bytes("cat", destination) != local.read_bytes():
        raise ValueError("server continuation readback differs")
    (STAGE / "server" / "eclipse-mounts.sql").write_text("".join((p / "server.sql").read_text(encoding="utf-8")
                                                                for p in sorted((STAGE / "packs").iterdir())), encoding="utf-8")
    print(f"server: {len(changed)} mounts renamed; spell continuation replaced (read at next restart)")


def learn_list():
    rows = []
    for pack in sorted((STAGE / "packs").iterdir()):
        m = json.loads((pack / "manifest.json").read_text())
        rows.append((m["target"]["name"], "Flying" if m["source"]["flying"] else "Ground", m["target"]["ids"]["Spell"], m["target"]["item_id"]))
    lines = [f"# Eclipse mount test commands\n\n{len(rows)} mounts imported from Eclipse. `.learn <spell>` teaches directly, "
             "`.additem <item>` gives the learning item, `.unlearn <spell>` removes it.\n\n| Mount | Type | Learn | Item |\n|---|---|---|---|"]
    lines += [f"| {a} | {b} | `.learn {c}` | `.additem {d}` |" for a, b, c, d in sorted(rows)]
    text = "\n".join(lines) + "\n"
    (STAGE / "eclipse-mount-learn-commands.md").write_text(text, encoding="utf-8")
    (Path(__file__).resolve().parents[2] / "docs/eclipse-mount-learn-commands.md").write_text(text, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--names", type=Path, required=True)
    parser.add_argument("--client", type=Path, action="append", default=[])
    args = parser.parse_args()
    names = json.loads(args.names.read_text(encoding="utf-8"))
    # Eclipse names can carry non-breaking spaces; match on the plain form.
    packs = {json.loads((p / "manifest.json").read_text())["target"]["name"].replace(" ", " "): p for p in (STAGE / "packs").iterdir()}
    missing = [k for k in names if k not in packs]
    if missing:
        raise SystemExit(f"names not found: {missing}")
    taken = set(packs) - set(names)
    clashes = [v for v in names.values() if v in taken]
    if clashes or len(set(names.values())) != len(names):
        raise SystemExit(f"new names collide: {clashes}")
    changed = {packs[old].name: rename_pack(packs[old], new) for old, new in names.items()}
    spell_names = {m["target"]["ids"]["Spell"]: m["target"]["name"] for m in changed.values()}
    for client in args.client:
        update_client(client, changed, spell_names)
    update_server(changed)
    learn_list()
