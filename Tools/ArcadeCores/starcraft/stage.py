"""Stage the StarCraft cabinet's game folder (what Tools/Arcade/pack.py ships as the "pc" system).

    python stage.py <StarCraft install> <bridge build dir> <out dir> [--instance X]

Copies only what the game needs (no docs, editor, profiles or third-party plugins), packs maps\\ into a stored
maps.zip (starcraft_core.cpp unpacks it), adds the bridge (ddraw.dll), and patches StarCraft.exe's single-instance mutex
so a cabinet runs even while the player has their own StarCraft open. --instance changes one letter of that name,
for a second copy on the same PC (the two-cabinet network test).
"""
import argparse
import shutil
import zipfile
from pathlib import Path

FILES = ["StarCraft.exe", "storm.dll", "Smackw32.dll", "Local.dll", "standard.snp", "battle.snp", "StarDat.mpq",
         "BrooDat.mpq", "patch_rt.mpq", "StarCraft.mpq", "BroodWar.mpq"]
MUTEX = b"Starcraft Check For Other Instances\0"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source", type=Path)
    ap.add_argument("bridge", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--instance", default="C", help="one letter: EsteriaS<letter> Check For Other Instances")
    args = ap.parse_args()
    assert len(args.instance) == 1 and args.instance.isalpha()
    args.out.mkdir(parents=True, exist_ok=True)
    for name in FILES:
        shutil.copy2(args.source / name, args.out / name)
    exe = bytearray((args.out / "StarCraft.exe").read_bytes())
    assert exe.count(MUTEX) == 1, "unexpected StarCraft.exe (not 1.16.1?)"
    at = exe.index(MUTEX)
    exe[at:at + len(MUTEX)] = (b"EsteriaS" + args.instance.encode() + MUTEX[9:])  # same length
    (args.out / "StarCraft.exe").write_bytes(exe)
    shutil.copy2(args.bridge / "ddraw.dll", args.out / "ddraw.dll")
    maps = args.source / "maps"
    with zipfile.ZipFile(args.out / "maps.zip", "w", zipfile.ZIP_STORED) as z:
        for f in sorted(maps.rglob("*")):
            if f.is_file():
                z.write(f, f.relative_to(maps).as_posix())
    print("staged", args.out)


if __name__ == "__main__":
    main()
