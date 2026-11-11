"""Shared install steps for Eclipse imports: asset MPQ, additive client registry, server rows and continuations."""
from __future__ import annotations

import ctypes as c
import json
import shutil
import subprocess
import time
from pathlib import Path

import eclipse_data as ed
import native_import as n
import native_mount as nm
from server import query

ALL_TABLES = {**n.TABLES, **n.CLIENT_TABLES}


def pack_assets(storm, sources: list[Path], names: list[str], output: Path, extra: dict[str, bytes] | None = None):
    """Copy the listed files out of the Eclipse archives (plus `extra` payloads) into one zlib-compressed MPQ."""
    extra = extra or {}
    handles = [storm.open_archive(p) for p in sources]
    handle = ed.cmp.H()
    if output.exists():
        raise SystemExit(f"{output} exists")
    temporary = output.with_suffix(".tmp")
    temporary.unlink(missing_ok=True)
    # LISTFILE | ATTRIBUTES | format v2 (the client already mounts >4 GB archives)
    if not storm.dll.SFileCreateArchive(str(temporary), 0x00100000 | 0x00200000 | 0x01000000, len(names) + len(extra) + 16, c.byref(handle)):
        raise OSError(f"SFileCreateArchive failed ({c.get_last_error()})")
    names = list(names) + list(extra)
    try:
        for name in names:
            payload = extra.get(name)
            for source in handles if payload is None else ():
                try:
                    payload = storm.read(source, name)
                    break
                except OSError:
                    continue
            if payload is None:
                raise FileNotFoundError(name)
            file_handle = ed.cmp.H()
            if not storm.dll.SFileCreateFile(handle, name.encode("ascii"), 0, len(payload), 0, 0x80000200, c.byref(file_handle)):
                raise OSError(f"SFileCreateFile failed: {name} ({c.get_last_error()})")
            try:
                if not storm.dll.SFileWriteFile(file_handle, c.create_string_buffer(payload or b"\0"), len(payload), 0x02):
                    raise OSError(f"SFileWriteFile failed: {name} ({c.get_last_error()})")
            finally:
                storm.dll.SFileFinishFile(file_handle)
    finally:
        storm.dll.SFileCloseArchive(handle)
        for source in handles:
            storm.dll.SFileCloseArchive(source)
    check = storm.open_archive(temporary)
    try:
        listed = {ed.norm(name) for name in ed.list_files(storm, check)}
        if not {ed.norm(name) for name in names} <= listed:
            raise ValueError("written archive is missing files")
    finally:
        storm.dll.SFileCloseArchive(check)
    temporary.replace(output)


def combine_with(base: Path | None, packs: list[Path], name: str) -> bytes:
    """`base` registry table (its rows and strings kept) plus every pack's rows; IDs must stay unique."""
    fields, string_fields = ALL_TABLES[name]
    rows, values = [], {}
    if base:
        base_rows, pool = n.wdbc(base.read_bytes(), fields)
        for index, row in enumerate(base_rows):
            rows.append(row)
            for field in string_fields:
                if row[field]:
                    values[index, field] = pool[row[field]:pool.index(0, row[field])].decode("utf-8")
    seen = {row[0] for row in rows}
    extra = n.wdbc(n.combine_tables(packs, name), fields)
    for row in extra[0]:
        if row[0] in seen:
            raise ValueError(f"{name}: ID {row[0]} already in the registry")
        index = len(rows)
        rows.append(row)
        for field in string_fields:
            if row[field]:
                values[index, field] = extra[1][row[field]:extra[1].index(0, row[field])].decode("utf-8")
    return n.make_wdbc(fields, rows, values)


def install_client(client: Path, stage: Path, archive_name: str, label: str):
    """Copy staged packs into Data/Retail, append their rows to the live registry, and drop in the asset MPQ."""
    root = client / "Data/Retail"
    packs = stage / "packs"
    archive = client / "Data" / archive_name
    built = stage / archive_name
    if archive.exists() and n.digest(archive.read_bytes()) != n.digest(built.read_bytes()):
        raise ValueError(f"{archive} exists with different content")
    backup = client / "_backups" / f"{label}-{time.strftime('%Y%m%d-%H%M%S')}"
    backup.mkdir(parents=True)
    for name in ("installed.json", "RetailCasc.ini"):
        shutil.copy2(root / name, backup / name)
    state = json.loads((root / "installed.json").read_text())
    ours = sorted(packs.iterdir())
    for pack in ours:
        manifest = n.load_pack(pack)
        destination = root / "Packs" / pack.name
        if destination.exists():
            if n.load_pack(destination)["definition_sha256"] != manifest["definition_sha256"]:
                raise ValueError(f"{destination} holds different content")
        else:
            shutil.copytree(pack, destination)
        state["packs"][pack.name] = {"source_key": manifest["source_key"], "definition_sha256": manifest["definition_sha256"]}
    # The live registry is not always a pure rebuild of the packs (hand-added aliases, packs without equipment.csv),
    # so append to it rather than recombining.
    config = dict(line.strip().split("=", 1) for line in (root / "RetailCasc.ini").read_text().splitlines() if "=" in line)
    current = root / config["tables"]
    revision = n.digest((config["tables"] + "".join(json.dumps(state["packs"][p.name], sort_keys=True) for p in ours)).encode())[:16]
    registry = root / "Registries" / revision
    if registry.exists():
        raise ValueError(f"{registry} already exists")
    staging = registry.with_name(revision + ".tmp")
    shutil.rmtree(staging, ignore_errors=True)
    shutil.copytree(current, staging)
    counts = {}
    for name in ALL_TABLES:
        path = staging / f"{name.lower()}.dbc"
        if not path.is_file() and not any((p / "tables" / f"{name.lower()}.dbc").is_file() for p in ours):
            continue
        merged = combine_with(path if path.is_file() else None, ours, name)
        counts[name] = len(n.wdbc(merged, ALL_TABLES[name][0])[0])
        path.write_bytes(merged)
    staging.rename(registry)
    lines = []
    for line in (root / "RetailCasc.ini").read_text().splitlines():
        if line.startswith("tables="):
            line = f"tables=Registries/{revision}"
        elif line.strip() == f"aliases={config['tables']}/aliases.csv":
            line = f"aliases=Registries/{revision}/aliases.csv"
        lines.append(line)
    (root / "RetailCasc.ini.tmp").write_text("\n".join(lines) + "\n", encoding="ascii")
    (root / "RetailCasc.ini.tmp").replace(root / "RetailCasc.ini")
    n.save_json(root / "installed.json", state)
    if not archive.exists():
        shutil.copy2(built, archive)
    print(json.dumps({"registry": revision, "rows": counts, "packs": len(state["packs"]), "archive": str(archive),
                      "backup": str(backup)}, indent=2))


def install_server(stage: Path, sql_name: str):
    """Publish the staged DBC continuations to the worldserver and apply the guarded SQL; verify every pack."""
    root = f"{nm.SERVER_DATA}/dbc-continuations"
    for path in sorted((stage / "server").glob("*.dbc1-*")):
        destination = f"{root}/{path.name}"
        exists = subprocess.run(["docker", "exec", "ac-worldserver", "test", "-e", destination]).returncode == 0
        if exists and nm.docker_bytes("cat", destination) != path.read_bytes():
            raise ValueError(f"{destination} exists with different content")
        if not exists:
            subprocess.run(["docker", "cp", str(path.resolve()), f"ac-worldserver:{destination}"], check=True, capture_output=True)
        if nm.docker_bytes("cat", destination) != path.read_bytes():
            raise ValueError(f"{destination} readback differs")
    query((stage / "server" / sql_name).read_text(encoding="utf-8"), timeout=3600)
    for pack in sorted((stage / "packs").iterdir()):
        n.verify_server(n.load_pack(pack))
    print("server rows and continuations installed; restart ac-worldserver to load the new spells")
