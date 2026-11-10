"""Add retail utility-mount props (Yak sidecars and stalls, Packmaster anvil and bag) as creature displays.

Usage: mount_passengers.py <client> <files-dir> <out-dir>
  <client>     the client root; its Data/Retail/RetailCasc.ini names the current table registry
  <files-dir>  the props' original retail files as <FileDataID>.bin (models, skins, textures)
Writes <out-dir>/registry (the current registry plus these rows: point RetailCasc.ini's tables= at it),
<out-dir>/server (dbc-continuation files) and <out-dir>/patch-ZzzzzzzzzzzzzzzzzzzzzzMountPassengers.MPQ.
shortcut: a native_import install rebuilds the registry from packs and drops these rows; rerun this script after one.
"""
import hashlib
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).parents[1] / "MountStudio"))
from native_import import MOUNT_TABLES, make_wdbc, wdbc  # noqa: E402

BUILD = "88d5e8ef05ec5a8446cdec93d142d7c6"
TEMPLATE_PACK = "grizzly-hills-packmaster"  # its CreatureModelData row is the template for the props
# display/model id: (model FileDataID, creature texture 1 or None, extra files)
PROPS = {
    205001: (614190, None, [614191]),             # travelersyakseat1 (sidecar)
    205002: (614192, None, [614193]),             # travelersyakseat2
    205003: (614195, None, [614196]),             # travelersyakvendor1 (goods stall)
    205004: (614197, None, [614198]),             # travelersyakvendor2
    205005: (5902794, 5901806, [5902807, 5902808, 5902809, 5902810]),  # bearmountutility_anvil
    205006: (5902795, 5901806, [5902811, 5902812, 5902813, 5902814]),  # bearmountutility_bag
}
SHARED = [614185, 614189, 614194]  # yak cart textures; the Packmaster's are already in its pack


def f32(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def main():
    client, files, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    retail = client / "Data" / "Retail"
    config = (retail / "RetailCasc.ini").read_text(encoding="ascii")
    tables = next(line.split("=", 1)[1] for line in config.splitlines() if line.startswith("tables="))
    source = retail / tables
    registry = out / "registry"
    if registry.exists():
        shutil.rmtree(registry)
    shutil.copytree(source, registry)
    template_rows, _ = wdbc((retail / "Packs" / TEMPLATE_PACK / "tables" / "creaturemodeldata.dbc").read_bytes(), 28)
    server = out / "server"
    server.mkdir(parents=True, exist_ok=True)
    new = {"CreatureModelData": ([], {}), "CreatureDisplayInfo": ([], {})}
    for display, (model, texture, _) in PROPS.items():
        row = template_rows[0].copy()
        row[0] = display
        new["CreatureModelData"][0].append(row)
        new["CreatureModelData"][1][len(new["CreatureModelData"][0]) - 1, 2] = f"retail\\{model}.m2"
        new["CreatureDisplayInfo"][0].append([display, display, 0, 0, f32(1), 255, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0])
        if texture:
            new["CreatureDisplayInfo"][1][len(new["CreatureDisplayInfo"][0]) - 1, 6] = f"{texture}.blp"
    for name, (rows, strings) in new.items():
        fields, string_fields = MOUNT_TABLES[name]
        path = registry / f"{name.lower()}.dbc"
        old_rows, pool = wdbc(path.read_bytes(), fields)
        if {r[0] for r in old_rows} & set(PROPS):
            raise SystemExit(f"{name}: prop IDs already in the registry")
        merged, values = [], {}
        for row in old_rows:
            for field in string_fields:
                if row[field]:
                    values[len(merged), field] = pool[row[field]:pool.index(0, row[field])].decode("utf-8")
            merged.append(row)
        for index, row in enumerate(rows):
            for field in string_fields:
                if (index, field) in strings:
                    values[len(merged), field] = strings[index, field]
            merged.append(row)
        path.write_bytes(make_wdbc(fields, merged, values))
        (server / f"{name}.dbc1-mount-passengers").write_bytes(make_wdbc(fields, rows, strings))
        assert len(wdbc(path.read_bytes(), fields)[0]) == len(old_rows) + len(PROPS)
    import worker
    ids = sorted({fid for model, _, extra in PROPS.values() for fid in [model, *extra]} | set(SHARED))
    entries = [(f"EsteriaRetail\\wow-{BUILD}\\2\\{fid}.bin", files / f"{fid}.bin",
                hashlib.sha256((files / f"{fid}.bin").read_bytes()).hexdigest()) for fid in ids]
    archive = out / "patch-ZzzzzzzzzzzzzzzzzzzzzzMountPassengers.MPQ"
    archive.unlink(missing_ok=True)
    worker.Storm(r"C:\Users\Zach\AppData\Local\Esteria Mount Studio\runtime\backend\StormLib.dll").pack(archive, entries)
    print("registry", registry, "files", len(entries))


if __name__ == "__main__":
    main()
