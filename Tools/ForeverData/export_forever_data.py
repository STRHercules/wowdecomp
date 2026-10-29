"""Writes forever_data, the WoW Forever lighting data Azeroth Remastered's grading and fog read
(WoW/Source/WorldClient/HD/HdForever.cpp), from an installed Forever client (wow_classic_beta).

    python export_forever_data.py "D:/Blizzard/World of Warcraft" "G:/3.3.5a - Dev/forever_data"

The install is only read: RetailAssetProbe (built from Tools/RetailAssets/probe.cpp) opens its CASC
storage in local mode. Written:

    hd_forever_light.txt    L light map slot0..slot7         Light: the 8 LightParams per light
    hd_forever_grading.txt  G setting time lut                LightData.ColorGradingFileDataID
    hd_forever_fog.txt      F setting time layer flags ...   LightDataGlobalVolumeFog, drawn layers (flag 8)
    grading/<lut>.blp       the LUTs those name (1024x32 uncompressed BLP2)

A 3.3.5 light gets Forever's settings when its ID and map match: the IDs of the lights both clients
have are the same lights (checked against the 3.3.5a Light.dbc). definitions/ holds the WoWDBDefs
layouts (github.com/wowdev/WoWDBDefs) the tables are read with; a newer Forever build may need newer ones.
"""

import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(REPO / "Tools/MountStudio/runtime/reader"))
from wotlkconv.db import db2, dbd  # noqa: E402

# DBFilesClient FileDataIDs (wowdev/wow-listfile).
TABLES = {"Light": 1375579, "LightData": 1375580, "LightDataGlobalVolumeFog": 7492879}
FOG = "Field_1_60_1_69876_{:03d}"
# HdForever.cpp's "F" line after setting, time and layer: flags, three colours, then the floats, by
# the column numbers its ForeverFogLayer comments give (start 5 ... exponent 26).
FOG_COLOURS = (1, 2, 3)
FOG_FLOATS = (5, 6, 7, 8, 10, 11, 12, 14, 15, 25, 26)
FOG_FLAGS, FOG_LAYER, FOG_DRAWN = 22, 23, 8


def probe(exe, config, name, destination, table):
    destination.unlink(missing_ok=True)
    flag = "--extract-table" if table else "--extract"
    done = subprocess.run([str(exe), str(config), name, flag, str(destination)], capture_output=True, text=True)
    if done.returncode:
        sys.exit(f"{name}: {(done.stderr or done.stdout).strip()}")


def lut_ok(data):
    """HdGrade.cpp's DecodeLut: BLP2, encoding 3 (BGRA), 1024x32, pixels inside the file."""
    if len(data) < 148 or data[:4] != b"BLP2" or data[8] != 3:
        return False
    width, height, offset = struct.unpack_from("<III", data, 12)
    return width == 1024 and height == 32 and 148 <= offset and offset + 1024 * 32 * 4 <= len(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("install", help="the WoW folder holding .build.info and Data")
    parser.add_argument("out", help="the forever_data folder to write (beside Wow.exe)")
    parser.add_argument("--probe", default=str(REPO / "build/retail-satchel/Release/RetailAssetProbe.exe"))
    args = parser.parse_args()
    exe, out = Path(args.probe), Path(args.out)
    if not exe.is_file():
        sys.exit(f"no RetailAssetProbe at {exe}: build the RetailAssetProbe target")
    if not (Path(args.install) / ".build.info").is_file():
        sys.exit(f"{args.install} has no .build.info")

    index = dbd.DbdIndex(HERE / "definitions")
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        config = temp / "ForeverLocal.ini"
        config.write_text(f"mode=local\npath={Path(args.install).as_posix()}\nproduct=wow_classic_beta\nlocale=2\n")
        tables = {}
        for name, file_id in TABLES.items():
            probe(exe, config, f"forever/{file_id}.db2", temp / f"{name}.db2", True)
            tables[name] = dict(db2.parse_db2((temp / f"{name}.db2").read_bytes(), name, index, name))

        light_data = tables["LightData"]
        (out / "grading").mkdir(parents=True, exist_ok=True)
        lines = [f"L {i} {r['ContinentID']} " + " ".join(map(str, r["LightParamsID"]))
                 for i, r in sorted(tables["Light"].items())]
        (out / "hd_forever_light.txt").write_text("\n".join(lines) + "\n")

        keys = sorted((r["LightParamID"], r["Time"], r["ColorGradingFileDataID"]) for r in light_data.values()
                      if r["ColorGradingFileDataID"])
        for lut in sorted({k[2] for k in keys}):
            path = out / "grading" / f"{lut}.blp"
            probe(exe, config, f"forever/{lut}.blp", path, False)
            if not lut_ok(path.read_bytes()):
                sys.exit(f"LUT {lut} is not a 1024x32 uncompressed BLP2; HdGrade.cpp would refuse it")
        (out / "hd_forever_grading.txt").write_text("".join(f"G {s} {t} {lut}\n" for s, t, lut in keys))

        fog = []
        for row in tables["LightDataGlobalVolumeFog"].values():
            flags = row[FOG.format(FOG_FLAGS)]
            data = light_data.get(row["LightDataID"])
            if not flags & FOG_DRAWN or data is None:
                continue
            colours = " ".join(f"{row[FOG.format(c)] & 0xFFFFFF:06x}" for c in FOG_COLOURS)
            floats = " ".join(f"{row[FOG.format(c)]:g}" for c in FOG_FLOATS)
            fog.append((data["LightParamID"], data["Time"], row[FOG.format(FOG_LAYER)], f"{flags} {colours} {floats}"))
        # HdForever.cpp starts a time key whenever the time changes, so each setting's lines go in time order.
        fog.sort(key=lambda f: f[:3])
        (out / "hd_forever_fog.txt").write_text("".join(f"F {s} {t} {layer} {rest}\n" for s, t, layer, rest in fog))

    print(f"{len(lines)} lights, {len(keys)} grading keys ({len({k[2] for k in keys})} LUTs), {len(fog)} fog layers -> {out}")


if __name__ == "__main__":
    main()
