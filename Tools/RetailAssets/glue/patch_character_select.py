"""Appends the character select skin (select_skin.lua) to the client's GlueXML CharacterSelect.lua.

Default is a dry run that writes the patched files to --stage. With --apply it rewrites Data\\patch-Z.MPQ and
Data\\enUS\\patch-enUS-Z.MPQ (close the game first). --restore puts the stock files saved in
backup\\charselect-stock back. Re-running replaces an older copy of the skin (marker to end of file).

  python patch_character_select.py --client "G:\\3.3.5a - Dev"            # dry run
  python patch_character_select.py --client "G:\\3.3.5a - Dev" --apply
  python patch_character_select.py --client "G:\\3.3.5a - Dev" --restore
"""

import argparse
import ctypes
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).parent
ENTRY = "Interface" + chr(92) + "GlueXML" + chr(92) + "CharacterSelect.lua"
ARCHIVES = (Path("Data") / "patch-Z.MPQ", Path("Data") / "Patch-ZzzzzzzzEthereal.MPQ", Path("Data") / "enUS" / "patch-enUS-Z.MPQ")
MARKER = "-- Character select skin (wowdecomp)"
SKIN = HERE / "select_skin.lua"
STOCK = HERE / "backup" / "charselect-stock"


def patch(text):
    nl = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    if MARKER in text:
        text = text[:text.index(MARKER)].rstrip("\n") + "\n"
    skin = SKIN.read_text(encoding="utf-8").replace("\r\n", "\n")
    return (text + "\n" + skin).replace("\n", nl)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--tools", type=Path, default=Path(r"R:\Users\Zach\Documents\GitHub\EsteriaWoW\tools"),
                        help="EsteriaWoW tools folder (provides the StormLib wrapper)")
    parser.add_argument("--stage", type=Path, default=HERE / "staged")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--restore", action="store_true")
    args = parser.parse_args()

    sys.path.insert(0, str(args.tools))
    from cars_mount_pack import DLL_DEFAULT, H, Storm  # noqa: E402

    storm = Storm(DLL_DEFAULT)
    args.stage.mkdir(parents=True, exist_ok=True)
    patched = {}
    for relative in ARCHIVES:
        path = args.client / relative
        handle = H()
        if not storm.dll.SFileOpenArchive(str(path), 0, 0x100, ctypes.byref(handle)):
            raise SystemExit("Cannot open " + str(path))
        try:
            original = storm.read(handle, ENTRY)
        finally:
            storm.dll.SFileCloseArchive(handle)
        if args.restore:
            result = (STOCK / (path.stem + "-CharacterSelect.lua")).read_bytes()
        else:
            result = patch(original.decode("utf-8-sig")).encode("utf-8")
            (args.stage / (path.stem + "-CharacterSelect.lua")).write_bytes(result)
        patched[path] = (original, result)
        print(path.name, "unchanged" if result == original else ("restore" if args.restore else "patched"))

    if not (args.apply or args.restore):
        print("Dry run. Staged files are in", args.stage)
        return
    running = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
    if "wowclient" in running or "wow.exe" in running or "wow64.exe" in running or "eclipse" in running:
        raise SystemExit("Close the game first; the archives are in use.")
    for path, (original, result) in patched.items():
        if result == original:
            continue
        storm.replace_archive_entries(path, {ENTRY: result})
        handle = H()
        storm.dll.SFileOpenArchive(str(path), 0, 0x100, ctypes.byref(handle))
        try:
            ok = storm.read(handle, ENTRY) == result
        finally:
            storm.dll.SFileCloseArchive(handle)
        print(path.name, "verified" if ok else "VERIFY FAILED")
        if not ok:
            raise SystemExit("Readback mismatch; run with --restore")


if __name__ == "__main__":
    main()
