r"""Translates the Azeroth Remastered materials' shader code into a client's Vulkan shader folders.

The client compiles the HD materials' HLSL at start-up with the system's d3dcompiler_47 (WoW/Source/WorldClient/HD/
HdTerrain.cpp and the like) and uses that code only when the Vulkan backend has its translated module. Code it does not
have is written to <client>\VulkanShaders*\hd-pending\<name>.bin (HdShaderSwap.cpp). This tool translates those blobs
with Tools/VulkanShaders (--extra-vertex/--extra-pixel), copies the modules into every VulkanShaders* folder of the client
and appends their manifest records, then removes the pending blobs. Restart the game afterwards.

    python Tools/HdMaterials/install_hd_shaders.py --client "G:/3.3.5a - Dev"

The modules match this machine's d3dcompiler_47: run it on the machine that plays.
"""
import argparse
import re
import shutil
import struct
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
FOLDERS = ("VulkanShaders", "VulkanShadersRetail")
VERTEX, PIXEL = 0xFFFE0300, 0xFFFF0300


def blocks_of(text):
    return [b.strip("\n") for b in re.split(r"\n\s*\n", text) if b.strip()]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--client", required=True, type=Path, help="the 3.3.5a client folder (holding Data and VulkanShaders*)")
    ap.add_argument("--glslang", default=str(REPO / "build/tools/glslang/bin/glslang.exe"))
    args = ap.parse_args()
    folders = [args.client / f for f in FOLDERS if (args.client / f / "manifest.txt").is_file()]
    if not folders:
        sys.exit(f"no VulkanShaders*/manifest.txt under {args.client}: run Tools/VulkanShaders/translate_shaders.py first")
    if not Path(args.glslang).is_file():
        sys.exit(f"no glslang at {args.glslang}")

    pending = {}
    for folder in folders:
        for blob in (folder / "hd-pending").glob("*.bin"):
            pending[blob.stem] = blob.read_bytes()
    if not pending:
        print("nothing pending: start the game once in visuals 2 so it writes the shaders it is missing")
        return

    sys.path.insert(0, str(REPO / "Tools/VulkanShaders"))
    from bls import translate
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        for kind in ("vertex", "pixel"):
            (temp / "in" / kind).mkdir(parents=True)
        for name, code in pending.items():
            version = struct.unpack_from("<I", code)[0] if len(code) >= 4 else 0
            if version not in (VERTEX, PIXEL):
                sys.exit(f"{name}: not vs_3_0 or ps_3_0 code")
            (temp / "in" / ("vertex" if version == VERTEX else "pixel") / f"{name}.bin").write_bytes(code)
        out = temp / "out"
        rc = translate.main(["--data", str(args.client / "Data"), "--glslang", args.glslang, "--out", str(out),
                             "--extra-vertex", str(temp / "in" / "vertex"), "--extra-pixel", str(temp / "in" / "pixel")])
        if rc:
            sys.exit(f"translation failed ({rc})")
        # Only the pending blobs' records (their names are the blob names); the stock ones are already installed.
        ours = [b for b in blocks_of((out / "manifest.txt").read_text(encoding="utf-8"))
                if b.startswith("record ") and any(re.search(rf"^name {re.escape(n)}(:\d+)?$", b, re.M) for n in pending)]
        translated = {n for n in pending if any(re.search(rf"^name {re.escape(n)}(:\d+)?$", b, re.M) for b in ours)}
        missing = sorted(set(pending) - translated)
        md5s = {b.split()[1] for b in ours}
        for folder in folders:
            manifest = folder / "manifest.txt"
            kept = [b for b in blocks_of(manifest.read_text(encoding="utf-8")) if not any(f"record {m} " in b for m in md5s)]
            for block in ours:
                kind, spv = block.split()[2], block.split()[4]
                (folder / kind).mkdir(exist_ok=True)
                shutil.copy2(out / spv, folder / spv)
            manifest.write_text("\n\n".join(kept + ours) + "\n", encoding="utf-8", newline="\n")
            for name in translated:
                (folder / "hd-pending" / f"{name}.bin").unlink(missing_ok=True)
            print(f"{folder}: {len(ours)} HD records")
    if missing:
        print(f"{len(missing)} could not be translated (see the translator's log above): {', '.join(missing[:8])}")
    print("restart the game")


if __name__ == "__main__":
    main()
