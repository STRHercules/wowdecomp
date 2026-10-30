"""Adds the Retail race option panel to the client's GlueXML CharacterCreate.lua.

The creator already draws up to 29 option rows from CycleCharCustomization("EA_GET", i). This patch makes any
race mapped to Retail data (native "EA_RETAIL" command) use that panel, shows the real choice names and
routes the randomizer to the native EA_RANDOM, binds viewport wheel input to
the native face zoom and right-drag to the native camera pan, then appends the Body page and the creator skin.
It is idempotent.

Default is a dry run that writes the patched files to --stage. With --apply it backs up and rewrites
Data\\patch-Z.MPQ and Data\\enUS\\patch-enUS-Z.MPQ. Close the game first.

  python patch_character_create.py --client "G:\\3.3.5a - Dev"            # dry run
  python patch_character_create.py --client "G:\\3.3.5a - Dev" --apply
"""

import argparse
import ctypes
import hashlib
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

import body_triangle

MARKER = "-- Retail race customization (wowdecomp)"
ENTRY = "Interface" + chr(92) + "GlueXML" + chr(92) + "CharacterCreate.lua"
# Later archives win, and these two carry their own copy of the creator (with the Ethereal portraits): patch them too.
ARCHIVES = (Path("Data") / "patch-Z.MPQ", Path("Data") / "enUS" / "patch-enUS-Z.MPQ",
            Path("Data") / "Patch-ZzzzzzzzEthereal.MPQ", Path("Data") / "Patch-ZzzzzzzzzzKyrianData.MPQ")
BODY_MARKER = "-- Body sliders page (wowdecomp)"
BODY_PAGE = Path(__file__).with_name("body_page.lua")
SKIN_MARKER = "-- Creator skin (wowdecomp)"
SKIN = Path(__file__).with_name("creator_skin.lua")


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit("Expected exactly one match for: " + old[:80])
    return text.replace(old, new)


def patch_base(text):
    if MARKER in text:
        return text
    nl = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    text = replace_once(text, "local function EA_ACTIVE()\n    return CharacterCreate.selectedRaceID == 20 or ",
        MARKER + "\n"
        "local function EA_IS_RETAIL()\n"
        "    return type(CycleCharCustomization) == \"function\" and CycleCharCustomization(\"EA_RETAIL\", 1) == 1;\n"
        "end\n"
        "local function EA_ACTIVE()\n    return EA_IS_RETAIL() or CharacterCreate.selectedRaceID == 20 or ")
    text = replace_once(text, "((CharacterCreate.selectedRaceID == 20 or CharacterCreate.selectedRaceID == 46",
        "((EA_IS_RETAIL() or CharacterCreate.selectedRaceID == 20 or CharacterCreate.selectedRaceID == 46")
    text = replace_once(text, "local function ChoiceText(id, value, label)\n",
        "local function ChoiceText(id, value, label)\n"
        "    if EA_IS_RETAIL() then\n"
        "        local retailName = CycleCharCustomization(\"EA_CHOICE_NAME\", id, value);\n"
        "        if retailName and retailName ~= \"\" then return retailName; end\n"
        "    end\n")
    text = replace_once(text, "    CharacterCustomization_CloseChoices();\n    randomize();\n",
        "    CharacterCustomization_CloseChoices();\n"
        "    if EA_IS_RETAIL() then\n"
        "        PlaySound(\"gsCharacterCreationLook\");\n"
        "        CycleCharCustomization(\"EA_RANDOM\", 1);\n"
        "        CharacterCreate_UpdateHairCustomization();\n"
        "        return;\n"
        "    end\n"
        "    randomize();\n")
    return text.replace("\n", nl)


def patch_capacity(text):
    text = patch_base(text)
    marker = "-- Retail option capacity v2 (45-option Dracthyr)"
    if marker in text:
        return text
    nl = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    # Keep stock/legacy control behavior; allocate enough frames and enumerate the actual retail count.
    start = text.index(MARKER)
    prefix, panel = text[:start], text[start:]
    panel = replace_once(panel, "local function EA_REFRESH()\n", marker + "\n"
        "local function EA_LIMIT()\n"
        "    if EA_IS_RETAIL() then return math.min(64, CycleCharCustomization(\"EA_COUNT\", 1) or 29); end\n"
        "    return 29;\nend\n"
        "local function EA_REFRESH()\n")
    panel = panel.replace("for i=6,29 do", "for i=6,64 do")
    panel = panel.replace("for i=1,29 do", "for i=1,64 do")
    panel = panel.replace("for i = 1, 29 do", "for i = 1, EA_LIMIT() do")
    panel = panel.replace("EA_ACTIVE() and 29 or 5", "EA_ACTIVE() and EA_LIMIT() or 5")
    panel = panel.replace("local rowSpacing=optionCount>22 and 28 or 36;",
        "local rowSpacing=math.min(optionCount>22 and 28 or 36, math.max(18, math.floor((GetScreenHeight()-210)/rowsPerColumn))); ")
    # EA_GET returns nil beyond the model's option count, hiding those frames after a race switch.
    return (prefix + panel).replace("\n", nl)


def patch(text):
    text = patch_capacity(text)
    marker = "-- Creator mousewheel face zoom (wowdecomp)"
    if marker in text:
        return text
    nl = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    text = replace_once(text, "function CharacterCreate_OnLoad(self)\n",
        marker + "\nfunction CharacterCreate_OnLoad(self)\n"
        "    -- The viewport owns zoom; choice pickers keep their own wheel scrolling.\n"
        "    CharacterCreateFrame:EnableMouseWheel(true);\n"
        "    CharacterCreateFrame:SetScript(\"OnMouseWheel\", function(_, delta)\n"
        "        CycleCharCustomization(\"EA_ZOOM\", delta);\n"
        "    end);\n")
    return text.replace("\n", nl)


def patch_pan(text):
    """Right-drag on the viewport pans the creator camera (native EA_PAN); right-click without moving resets it."""
    marker = "-- Creator right-drag camera pan (wowdecomp)"
    if marker in text:
        return text
    nl = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    text = replace_once(text, "function CharacterCreateFrame_OnMouseDown(button)\n",
        marker + "\n"
        "function CharacterCreateFrame_OnMouseDown(button)\n"
        "    if ( button == \"RightButton\" ) then\n"
        "        CHARACTER_CREATE_PAN_X, CHARACTER_CREATE_PAN_Y = GetCursorPosition();\n"
        "        CHARACTER_CREATE_PAN_MOVED = false;\n"
        "    end\n")
    text = replace_once(text, "function CharacterCreateFrame_OnMouseUp(button)\n",
        "function CharacterCreateFrame_OnMouseUp(button)\n"
        "    if ( button == \"RightButton\" and CHARACTER_CREATE_PAN_X ) then\n"
        "        if ( not CHARACTER_CREATE_PAN_MOVED ) then CycleCharCustomization(\"EA_PAN_RESET\", 1); end\n"
        "        CHARACTER_CREATE_PAN_X = nil;\n"
        "    end\n")
    text = replace_once(text, "function CharacterCreateFrame_OnUpdate()\n",
        "function CharacterCreateFrame_OnUpdate()\n"
        "    if ( CHARACTER_CREATE_PAN_X ) then\n"
        "        -- Glue GetCursorPosition and GetScreenHeight share units: pass screen-height fractions.\n"
        "        local x, y = GetCursorPosition();\n"
        "        local dx, dy = x - CHARACTER_CREATE_PAN_X, y - CHARACTER_CREATE_PAN_Y;\n"
        "        if ( dx ~= 0 or dy ~= 0 ) then\n"
        "            CHARACTER_CREATE_PAN_MOVED = true;\n"
        "            CycleCharCustomization(\"EA_PAN\", dx / GetScreenHeight(), dy / GetScreenHeight());\n"
        "            CHARACTER_CREATE_PAN_X, CHARACTER_CREATE_PAN_Y = x, y;\n"
        "        end\n"
        "    end\n")
    return text.replace("\n", nl)


def patch_body_page(text):
    """Adds the Body sliders page: hides the appearance rows while it is open, then appends the page itself.

    Re-running replaces an older copy of the page (everything from the marker to the end of the file).
    """
    nl = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    guard = "    if BodyShapePage_Active and BodyShapePage_Active() then\n"
    if guard not in text:
        text = replace_once(text, "local function EA_REFRESH()\n    if not CharacterCustomizationButtonFrame1 then return; end\n",
            "local function EA_REFRESH()\n    if not CharacterCustomizationButtonFrame1 then return; end\n"
            + guard +
            "        for i=1,64 do\n"
            "            local row = _G[\"CharacterCustomizationButtonFrame\"..i];\n"
            "            if row then row:Hide(); end\n"
            "        end\n"
            "        return;\n"
            "    end\n")
    if BODY_MARKER in text:
        text = text[:text.index(BODY_MARKER)].rstrip("\n") + "\n"
    page = BODY_PAGE.read_text(encoding="utf-8").replace("\r\n", "\n")
    if not text.endswith("\n"):
        text += "\n"
    return (text + "\n" + page).replace("\n", nl)


def patch_skin(text):
    """Appends the creator skin after the Body page (re-running replaces an older copy, marker to end of file)."""
    nl = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    if SKIN_MARKER in text:
        text = text[:text.index(SKIN_MARKER)].rstrip("\n") + "\n"
    skin = SKIN.read_text(encoding="utf-8").replace("\r\n", "\n")
    return (text + "\n" + skin).replace("\n", nl)


def patch_rows(text):
    """Races on the stock option layout (not EA_ACTIVE) placed their rows 900px right of centre, which clips off the
    creator's right panel on some screens. Pin them to the panel the way the Esteria layout does (55px in)."""
    old = 'frame:SetPoint("CENTER", parent, "CENTER", 900, 200-i*50);'
    if old not in text:
        return text
    return replace_once(text, old, 'frame:SetPoint("RIGHT", CharacterCreateFrame, "RIGHT", -55, 200-i*50);')


def patch_all(text):
    # The Body page strips from its marker to the end of the file, so the skin is always appended after it.
    # Other packers (Ethereal portraits) append lines after the skin's closing "end)();"; keep them at the end.
    skin_at = text.find(SKIN_MARKER)
    tail = text[text.rindex("end)();") + len("end)();"):] if skin_at >= 0 and "end)();" in text[skin_at:] else ""
    patched = patch_skin(patch_body_page(patch_pan(patch_rows(patch(text)))))
    return patched.rstrip("\r\n") + tail if tail.strip() else patched


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--tools", type=Path, default=Path(r"R:\Users\Zach\Documents\GitHub\EsteriaWoW\tools"),
                        help="EsteriaWoW tools folder (provides the StormLib wrapper)")
    parser.add_argument("--stage", type=Path, default=Path(__file__).with_name("staged"))
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--no-backup", action="store_true",
                        help="skip the archive backup (each is several GB); only when an earlier backup of the "
                             "unpatched archives already exists")
    args = parser.parse_args()

    sys.path.insert(0, str(args.tools))
    from cars_mount_pack import DLL_DEFAULT, H, Storm  # noqa: E402

    storm = Storm(DLL_DEFAULT)
    args.stage.mkdir(parents=True, exist_ok=True)
    triangle, _ = body_triangle.build()
    patched = {}
    for relative in ARCHIVES:
        path = args.client / relative
        handle = H()
        if not storm.dll.SFileOpenArchive(str(path), 0, 0x100, ctypes.byref(handle)):
            raise SystemExit("Cannot open " + str(path))
        try:
            original = {ENTRY: storm.read(handle, ENTRY)}
            # Textures are locale-independent: the body triangle ships in the general archive only.
            if relative == ARCHIVES[0]:
                try:
                    original[body_triangle.ENTRY] = storm.read(handle, body_triangle.ENTRY)
                except OSError:
                    original[body_triangle.ENTRY] = b""
        finally:
            storm.dll.SFileCloseArchive(handle)
        result = {ENTRY: patch_all(original[ENTRY].decode("utf-8")).encode("utf-8")}
        if body_triangle.ENTRY in original:
            result[body_triangle.ENTRY] = triangle
        (args.stage / (path.stem + "-CharacterCreate.lua")).write_bytes(result[ENTRY])
        patched[path] = (original, result)
        changed = [name.rsplit(chr(92), 1)[-1] for name in result if result[name] != original[name]]
        print(path.name, "already patched" if not changed else "patched: " + ", ".join(changed))

    if not args.apply:
        print("Dry run. Staged files are in", args.stage)
        return
    running = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
    if "wowclient" in running or "wow.exe" in running or "wow64.exe" in running or "eclipse" in running:
        raise SystemExit("Close the game first; the archives are in use.")
    backup = args.client / "_backups" / ("retail-glue-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
    if not args.no_backup:
        backup.mkdir(parents=True)
    for path, (original, result) in patched.items():
        if result == original:
            continue
        if args.no_backup:
            print("no backup of", path.name, "(--no-backup)")
        else:
            shutil.copy2(path, backup / path.name)
            print("backup", backup / path.name, hashlib.sha256((backup / path.name).read_bytes()).hexdigest()[:16])
        storm.replace_archive_entries(path, result)
        handle = H()
        storm.dll.SFileOpenArchive(str(path), 0, 0x100, ctypes.byref(handle))
        try:
            ok = all(storm.read(handle, name) == data for name, data in result.items())
        finally:
            storm.dll.SFileCloseArchive(handle)
        print(path.name, "verified" if ok else "VERIFY FAILED")
        if not ok:
            raise SystemExit("Readback mismatch; restore from " + str(backup))


if __name__ == "__main__":
    main()
