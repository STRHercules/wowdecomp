"""Install build.py outputs into the Dev client and the housing stage realm. Backs up everything it replaces.

    python install.py <slug>=<out_dir> [<slug>=<out_dir> ...]

1. <client>/Data/Patch-ZzzzzzzzzzzEsteriaArcade.MPQ (art): the archive's current files plus these props', read back.
2. Each display row into the active supplemental gameobjectdisplayinfo.dbc (RetailCasc.ini `tables=`), replacing by ID.
3. vmap4_assembler (in the housing-build container) over the colliding props, merged into GameObjectModels.dtree in
   the shared esteriawow_ac-client-data volume (entries replaced by display ID; purely additive for other realms).
4. Each prop.sql on housing_dev_world. Restart housing-stage afterwards.
"""
import json
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CLIENT = Path("G:/3.3.5a - Dev")
ARCHIVE = CLIENT / "Data/Patch-ZzzzzzzzzzzEsteriaArcade.MPQ"
BACKUP = CLIENT / "_backups" / ("props-" + time.strftime("%Y%m%d-%H%M%S"))
VOLUME = "esteriawow_ac-client-data"
os.environ["MSYS_NO_PATHCONV"] = "1"
sys.path.insert(0, str(Path(__file__).parent))
from build import PROPS  # noqa: E402


def mysql(sql, db="housing_dev_world"):
    r = subprocess.run(["docker", "exec", "-i", "ac-database", "sh", "-c", f'mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -N {db}'],
                       input=sql.encode(), capture_output=True)
    if r.returncode:
        raise SystemExit(r.stderr.decode())
    return r.stdout.decode()


def merge_dbc(base, extra):
    """base + extra's single row, replacing any row with the same ID."""
    n, f, rs, ss = struct.unpack_from("<4I", base, 4)
    _, f2, rs2, _ = struct.unpack_from("<4I", extra, 4)
    assert (f, rs) == (f2, rs2) == (19, 76)
    new_id = struct.unpack_from("<I", extra, 20)[0]
    rows = [base[20 + i * rs:20 + (i + 1) * rs] for i in range(n)]
    rows = [r for r in rows if struct.unpack_from("<I", r)[0] != new_id]
    new = bytearray(extra[20:20 + rs])
    struct.pack_into("<I", new, 4, struct.unpack_from("<I", new, 4)[0] + ss)  # string offset into appended pool
    pool = base[20 + n * rs:] + extra[20 + rs:]
    return b"WDBC" + struct.pack("<4I", len(rows) + 1, f, rs, len(pool)) + b"".join(rows) + bytes(new) + pool


def dtree_entries(data):
    """GameObjectModels.dtree: 'VMAP_4.8' then (u32 display, u8 isWmo, u32 len, name, 2 x Vector3 bounds) entries."""
    assert data[:8] == b"VMAP_4.8"
    at, out = 8, []
    while at < len(data):
        display, _, size = struct.unpack_from("<IBI", data, at)
        end = at + 9 + size + 24
        out.append((display, data[at:end]))
        at = end
    assert at == len(data)
    return out


def main():
    builds = {a.split("=", 1)[0]: Path(a.split("=", 1)[1]) for a in sys.argv[1:]}
    assert builds and all(s in PROPS for s in builds), list(builds)
    BACKUP.mkdir(parents=True)

    # 1. art MPQ: keep what is there (the Lionheart cabinet, other props), add or replace these props' files
    sys.path.insert(0, str(REPO.parent / "EsteriaWoW/tools"))
    from cars_mount_pack import Storm
    storm = Storm(REPO / "Tools/MountStudio/runtime/backend/StormLib.dll")
    files = {}
    if ARCHIVE.exists():
        handle = storm.open_archive(ARCHIVE)
        try:
            for name, *_ in storm.list_files(handle):
                if not name.startswith("("):  # (listfile), (attributes)
                    files[name] = storm.read(handle, name)
        finally:
            storm.dll.SFileCloseArchive(handle)
        shutil.copy2(ARCHIVE, BACKUP / ARCHIVE.name)
    for out in builds.values():
        files.update({k: Path(v).read_bytes() for k, v in json.loads((out / "files.json").read_text()).items()})
    fresh = ARCHIVE.with_name(ARCHIVE.stem + "-new.MPQ")  # built beside the live one, swapped in once complete
    fresh.unlink(missing_ok=True)
    storm.create_archive(fresh, files)
    os.replace(fresh, ARCHIVE)
    handle = storm.open_archive(ARCHIVE)
    try:
        for name, data in files.items():
            assert storm.read(handle, name) == data, name
    finally:
        storm.dll.SFileCloseArchive(handle)
    print("mpq", ARCHIVE, len(files), "files")

    # 2. client display rows
    ini = (CLIENT / "Data/Retail/RetailCasc.ini").read_text()
    tables = next(line.split("=", 1)[1].strip() for line in ini.splitlines() if line.startswith("tables="))
    dbc = CLIENT / "Data/Retail" / tables / "gameobjectdisplayinfo.dbc"
    shutil.copy2(dbc, BACKUP / "gameobjectdisplayinfo.dbc")
    merged = dbc.read_bytes()
    for out in builds.values():
        merged = merge_dbc(merged, (out / "gameobjectdisplayinfo-row.dbc").read_bytes())
    n, _, rs, ss = struct.unpack_from("<4I", merged, 4)
    assert len(merged) == 20 + n * rs + ss
    dbc.write_bytes(merged)
    print("registry", dbc, n, "rows")

    # 3. vmap: assemble the colliding props together, then merge into the shared dtree by display ID
    colliding = {s: o for s, o in builds.items() if (o / "vmap-src").exists()}
    if colliding:
        src = BACKUP / "vmap-src"
        src.mkdir()
        (src / "dir_bin").write_bytes(b"")
        listing = b"".join((o / "vmap-src/temp_gameobject_models").read_bytes()[8:] for o in colliding.values())
        (src / "temp_gameobject_models").write_bytes(b"VMAP048\0" + listing)
        for s, o in colliding.items():
            shutil.copy2(o / "vmap-src" / f"{s}.m2", src / f"{s}.m2")
        run = lambda *a: subprocess.run(["docker", *a], check=True, capture_output=True)
        run("exec", "housing-build", "sh", "-c", "rm -rf /tmp/props && mkdir -p /tmp/props/out")
        run("cp", str(src), "housing-build:/tmp/props/src")
        run("exec", "housing-build", "sh", "-c", "cd /tmp/props && /azerothcore/env/dist/bin/vmap4_assembler src out")
        run("cp", "housing-build:/tmp/props/out", str(BACKUP / "vmap-out"))
        volume = ["docker", "run", "--rm", "-i", "-v", f"{VOLUME}:/data", "alpine"]
        dtree = subprocess.run(volume + ["cat", "/data/vmaps/GameObjectModels.dtree"], capture_output=True, check=True).stdout
        (BACKUP / "GameObjectModels.dtree").write_bytes(dtree)
        ours = dtree_entries((BACKUP / "vmap-out/GameObjectModels.dtree").read_bytes())
        mine = {d for d, _ in ours}
        assert mine == {PROPS[s]["display"] for s in colliding}, mine
        entries = [e for d, e in dtree_entries(dtree) if d not in mine] + [e for _, e in ours]
        dtree = b"VMAP_4.8" + b"".join(entries)
        subprocess.run(volume + ["sh", "-c", "cat > /data/vmaps/GameObjectModels.dtree"], input=dtree, check=True)
        for s in colliding:
            vmo = (BACKUP / f"vmap-out/{s}.m2.vmo").read_bytes()
            subprocess.run(volume + ["sh", "-c", f"cat > /data/vmaps/{s}.m2.vmo"], input=vmo, check=True)
        print("vmap", len(entries), "dtree entries;", ", ".join(colliding))

    # 4. SQL
    displays = ",".join(str(PROPS[s]["display"]) for s in builds)
    (BACKUP / "world-before.sql").write_text(mysql(
        f"SELECT * FROM gameobjectdisplayinfo_dbc WHERE ID IN ({displays}); "
        f"SELECT * FROM gameobject_template WHERE displayId IN ({displays});"))
    mysql("".join((o / "prop.sql").read_text() for o in builds.values()))
    print("sql applied; backups in", BACKUP)


if __name__ == "__main__":
    main()
