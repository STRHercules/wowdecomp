"""Install the build.py output into the Dev client and the housing stage realm. Backs up everything it replaces.

    python install.py <out_dir> [--spawn-near <character name>]

1. <client>/Data/Patch-ZzzzzzzzzzzEsteriaArcade.MPQ (art), read back through StormLib.
2. Appends the display row to the active supplemental gameobjectdisplayinfo.dbc (RetailCasc.ini `tables=`).
3. Appends the vmap entry to vmaps/GameObjectModels.dtree and adds LionheartArcade.m2.vmo in the shared
   esteriawow_ac-client-data volume (purely additive; no other realm uses display 3610001).
4. arcade.sql (+ optional spawn) on housing_dev_world. Restart housing-stage afterwards.
"""
import json
import math
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
BACKUP = CLIENT / "_backups" / ("arcade-" + time.strftime("%Y%m%d-%H%M%S"))
VOLUME = "esteriawow_ac-client-data"
os.environ["MSYS_NO_PATHCONV"] = "1"
sys.path.insert(0, str(Path(__file__).parent))
from build import DISPLAY_ID, GO_ENTRY  # noqa: E402


def mysql(sql, db="housing_dev_world"):
    r = subprocess.run(["docker", "exec", "-i", "ac-database", "sh", "-c", f'mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -N {db}'],
                       input=sql.encode(), capture_output=True)
    if r.returncode:
        raise SystemExit(r.stderr.decode())
    return r.stdout.decode()


def merge_dbc(base, extra):
    n, f, rs, ss = struct.unpack_from("<4I", base, 4)
    n2, f2, rs2, ss2 = struct.unpack_from("<4I", extra, 4)
    assert (f, rs) == (f2, rs2) == (19, 76)
    rows = [base[20 + i * rs:20 + (i + 1) * rs] for i in range(n)]
    if any(struct.unpack_from("<I", r)[0] == DISPLAY_ID for r in rows):
        rows = [r for r in rows if struct.unpack_from("<I", r)[0] != DISPLAY_ID]  # re-install replaces our row
    new = bytearray(extra[20:20 + rs])
    struct.pack_into("<I", new, 4, struct.unpack_from("<I", new, 4)[0] + ss)  # string offset into appended pool
    pool = base[20 + n * rs:] + extra[20 + rs:]
    return b"WDBC" + struct.pack("<4I", len(rows) + 1, f, rs, len(pool)) + b"".join(rows) + bytes(new) + pool


def main():
    out = Path(sys.argv[1])
    BACKUP.mkdir(parents=True)

    # 1. art MPQ
    files = {k: Path(v).read_bytes() for k, v in json.loads((out / "files.json").read_text()).items()}
    sys.path.insert(0, str(REPO.parent / "EsteriaWoW/tools"))
    from cars_mount_pack import Storm
    storm = Storm(REPO / "Tools/MountStudio/runtime/backend/StormLib.dll")
    if ARCHIVE.exists():
        shutil.move(ARCHIVE, BACKUP / ARCHIVE.name)
    storm.create_archive(ARCHIVE, files)
    handle = storm.open_archive(ARCHIVE)
    try:
        for name, data in files.items():
            assert storm.read(handle, name) == data, name
    finally:
        storm.dll.SFileCloseArchive(handle)
    print("mpq", ARCHIVE)

    # 2. client display row
    ini = (CLIENT / "Data/Retail/RetailCasc.ini").read_text()
    tables = next(line.split("=", 1)[1].strip() for line in ini.splitlines() if line.startswith("tables="))
    dbc = CLIENT / "Data/Retail" / tables / "gameobjectdisplayinfo.dbc"
    shutil.copy2(dbc, BACKUP / "gameobjectdisplayinfo.dbc")
    merged = merge_dbc(dbc.read_bytes(), (out / "gameobjectdisplayinfo-row.dbc").read_bytes())
    n, _, rs, ss = struct.unpack_from("<4I", merged, 4)
    pool, last = 20 + n * rs, 20 + (n - 1) * rs
    assert len(merged) == pool + ss and struct.unpack_from("<I", merged, last)[0] == DISPLAY_ID
    name_at = pool + struct.unpack_from("<I", merged, last + 4)[0]
    assert merged[name_at:merged.index(b"\0", name_at)] == b"World\\Esteria\\Arcade\\LionheartArcade.m2"
    dbc.write_bytes(merged)
    print("registry", dbc)

    # 3. vmap: pull current dtree, append our entries, push dtree + vmo
    volume = ["docker", "run", "--rm", "-i", "-v", f"{VOLUME}:/data", "alpine"]
    dtree = subprocess.run(volume + ["cat", "/data/vmaps/GameObjectModels.dtree"], capture_output=True, check=True).stdout
    assert dtree[:8] == b"VMAP_4.8"
    (BACKUP / "GameObjectModels.dtree").write_bytes(dtree)
    ours = (out / "vmap-out/GameObjectModels.dtree").read_bytes()
    assert ours[:8] == b"VMAP_4.8"
    entry = struct.pack("<IBI", DISPLAY_ID, 0, 18) + b"LionheartArcade.m2"
    if entry in dtree:  # re-install: drop the old entry (prefix + two Vector3 bounds) so bounds stay current
        at = dtree.index(entry)
        dtree = dtree[:at] + dtree[at + len(entry) + 24:]
    dtree += ours[8:]
    subprocess.run(volume + ["sh", "-c", "cat > /data/vmaps/GameObjectModels.dtree"], input=dtree, check=True)
    vmo = (out / "vmap-out/LionheartArcade.m2.vmo").read_bytes()
    subprocess.run(volume + ["sh", "-c", "cat > /data/vmaps/LionheartArcade.m2.vmo"], input=vmo, check=True)
    print("vmap", len(dtree), "bytes dtree, vmo", len(vmo))

    # 4. SQL (+ spawn three yards in front of the character, facing them)
    (BACKUP / "world-before.sql").write_text(mysql(
        f"SELECT * FROM gameobject_template WHERE entry={GO_ENTRY}; SELECT * FROM gameobject WHERE id={GO_ENTRY};"))
    sql = (out / "arcade.sql").read_text()
    if "--spawn-near" in sys.argv:
        who = sys.argv[sys.argv.index("--spawn-near") + 1].replace("'", "")
        row = mysql(f"SELECT map,position_x,position_y,position_z,orientation FROM characters WHERE name='{who}';",
                    "housing_dev_characters").split()
        m, x, y, z, o = int(row[0]), *map(float, row[1:])
        x, y, face = x + 3 * math.cos(o), y + 3 * math.sin(o), (o + math.pi) % (2 * math.pi)
        sql += (f"DELETE FROM `gameobject` WHERE `id` = {GO_ENTRY};\n"
                f"INSERT INTO `gameobject` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, "
                f"`position_z`, `orientation`, `rotation2`, `rotation3`, `spawntimesecs`, `animprogress`, `state`, `Comment`) "
                f"VALUES ({GO_ENTRY}, {GO_ENTRY}, {m}, 1, 1, {x:.3f}, {y:.3f}, {z:.3f}, {face:.4f}, {math.sin(face / 2):.6f}, "
                f"{math.cos(face / 2):.6f}, 0, 100, 1, 'Lionheart Arcade Cabinet');\n")
    mysql(sql)
    print("sql applied; backups in", BACKUP)


if __name__ == "__main__":
    main()
