"""Pack Addons/EsteriaEmotes into its own client archive (separate from the shell MPQ, so it deploys and rolls back
alone). The name avoids "EsteriaEmote", which install_emote_pack.py treats as an older emote pack to move aside.

  python build_mpq.py --out "G:\\3.3.5a - Dev\\Data\\Patch-ZzzzzzzzzzzEsteriaDanceUI.MPQ"
"""
import argparse
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, r"R:\Users\Zach\Documents\GitHub\EsteriaWoW\tools")
import cars_mount_pack  # noqa: E402

STORM_DLL = REPO / "Tools" / "MountStudio" / "runtime" / "backend" / "StormLib.dll"
FILES = ("EsteriaEmotes.toc", "Bindings.xml", "esteria_style.lua", "EsteriaEmotesData.lua", "EsteriaEmotes.lua",
         "EsteriaEmotesWheel.lua")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", required=True)
    out = pathlib.Path(ap.parse_args().out)
    if "esteriaemote" in out.name.lower():
        raise SystemExit("Archive name must not contain 'EsteriaEmote' (emote-pack installer would move it aside)")
    entries = {"Interface\\AddOns\\EsteriaEmotes\\" + name: (HERE / name).read_bytes() for name in FILES}
    if out.exists():
        raise SystemExit(f"{out} exists; move it aside first")
    cars_mount_pack.Storm(STORM_DLL).create_archive(out, entries)
    print(f"{out}: {len(entries)} files, {out.stat().st_size} bytes")


if __name__ == "__main__":
    main()
