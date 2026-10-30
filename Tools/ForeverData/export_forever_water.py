"""Writes the WoW Forever water data Azeroth Remastered's water reads (WoW/Source/WorldClient/HD/HdWater.cpp,
HdWaterWaves.cpp) from an installed Forever client (wow_classic_beta), into the forever_data folder export_forever_data.py fills.

    python export_forever_water.py "D:/Blizzard/World of Warcraft" "G:/3.3.5a - Dev/forever_data"

The install is only read (RetailAssetProbe, CASC local mode). Written:

    hd_forever_liquid.txt  T type <28 numbers>   one line per Forever "PBRWater" LiquidType: the 7 shader rows r0 r1 r2 r3 r4 r5
                                                 r10 of the pixel shader's cType[], 4 numbers each
    hd_forever_fft.txt     F id size seed wind gamma dir1 fetch1 spread1 dir2 fetch2 spread2   one line per FFTTile
                           L type tile tile ...                                                the tiles of each liquid type

Forever's water has no table of its own in WoWDBDefs: its parameters are columns of LiquidType (build 1.60.1: 3 colours and
38 floats per row; the PBR types are IDs 12xx named "PBRWater - ..."). The mapping to the shader's rows was found by matching
the donor's own built-in rows against the real ones (the ocean's agree to a few per cent):

    r0  absorption rgb = -ln(linear Color[0]), density = Float[0]      (T = exp(-depth * density * absorption))
    r1  Float[1] NoV^2 weight, Float[3] slope variance, Float[2] isotropic, Float[4] turbulence
    r2  crest tint = linear Color[1], wave height range = Float[5]
    r3  base tint = linear Color[2], Henyey-Greenstein g = Float[23]
    r4  depth foam: strength Float[11], tiling Float[8], range Float[9], speed Float[10]
    r5  rim foam:   strength Float[15], tiling Float[12], range Float[13], speed Float[14]
    r10 sun roughness Float[26], sky roughness Float[27], sky strength Float[25], sun specular scale 4

The FFTTile columns Field_000..009 are size, seed, wind speed, JONSWAP gamma, then two groups of direction, fetch (km) and
spreading (a group with fetch <= 0 is unused). Their meaning is inferred, as HdWaterWaves.h says.
definitions/ holds the WoWDBDefs layouts (LiquidType, FFTTile, LiquidTypeXFFTTile for build 1.60.1).
"""

import argparse
import math
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(REPO / "Tools/MountStudio/runtime/reader"))
from wotlkconv.db import db2, dbd  # noqa: E402

# DBFilesClient FileDataIDs (wowdev/wow-listfile).
TABLES = {"LiquidType": 1371380, "FFTTile": 7489860, "LiquidTypeXFFTTile": 7489861}
GAMMA = 2.2  # the pixel shader's BgLinear / ToLinear


def linear(colour):
    """An ARGB int (signed, as the table stores it) to linear rgb."""
    u = colour & 0xFFFFFFFF
    return [((u >> shift) & 255) / 255.0 for shift in (16, 8, 0)]


def to_linear(rgb):
    return [c ** GAMMA for c in rgb]


def type_rows(row):
    f, c = row["Float"], row["Color"]
    c0, c1, c2 = to_linear(linear(c[0])), to_linear(linear(c[1])), to_linear(linear(c[2]))
    absorb = [-math.log(max(v, 1e-3)) for v in c0]
    return [
        absorb + [f[0]],
        [f[1], f[3], f[2], f[4]],
        c1 + [f[5]],
        c2 + [f[23]],
        [f[11], f[8], f[9], f[10]],
        [f[15], f[12], f[13], f[14]],
        [f[26], f[27], f[25], 4.0],
    ]


def probe(exe, config, file_id, destination):
    destination.unlink(missing_ok=True)
    done = subprocess.run([str(exe), str(config), f"forever/{file_id}.db2", "--extract-table", str(destination)], capture_output=True, text=True)
    if done.returncode:
        sys.exit(f"{file_id}: {(done.stderr or done.stdout).strip()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("install", help="the WoW folder holding .build.info and Data")
    parser.add_argument("out", help="the forever_data folder to write (beside Wow.exe)")
    parser.add_argument("--probe", default=str(REPO / "build/retail-assets/Release/RetailAssetProbe.exe"))
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
            probe(exe, config, file_id, temp / f"{name}.db2")
            tables[name] = dict(db2.parse_db2((temp / f"{name}.db2").read_bytes(), name, index, name))

    out.mkdir(parents=True, exist_ok=True)
    pbr = {k: r for k, r in tables["LiquidType"].items() if r["Name"].startswith("PBRWater")}
    lines = []
    for k, row in sorted(pbr.items()):
        numbers = [v for r in type_rows(row) for v in r]
        lines.append(f"T {k} " + " ".join(f"{v:.6g}" for v in numbers) + f"  # {row['Name']}\n")
    (out / "hd_forever_liquid.txt").write_text("# V 1: T type, 28 numbers (see export_forever_water.py)\n" + "".join(lines))

    fft = []
    for k, r in sorted(tables["FFTTile"].items()):
        v = [r[f"Field_1_60_1_69876_{i:03d}"] for i in range(10)]
        fft.append(f"F {k} " + " ".join(f"{x:.9g}" for x in v) + "\n")
    by_type = {}
    for r in tables["LiquidTypeXFFTTile"].values():
        by_type.setdefault(r["LiquidTypeID"], []).append(r["FFTTileID"])
    for t, tiles in sorted(by_type.items()):
        fft.append(f"L {t} " + " ".join(str(i) for i in sorted(set(tiles))) + "\n")
    (out / "hd_forever_fft.txt").write_text("".join(fft))
    print(f"{len(pbr)} PBR water types, {len(tables['FFTTile'])} FFT tiles, {len(by_type)} liquid types with tiles -> {out}")


if __name__ == "__main__":
    main()
