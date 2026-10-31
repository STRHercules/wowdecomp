"""Builds Patch-ZzzzzzzzzzzEsteriaShell.MPQ: the Esteria additions to the DragonUI micro strip, the Everlook radio
add-on and the transmog window, as one archive that sorts after every other patch.

  python build_shell_mpq.py --client "G:\\3.3.5a - Dev" --out <archive path>

DragonUI is read out of the client's own DragonUI archive and patched by exact string replacement (each replacement
must match the expected number of times, so a DragonUI update fails loudly instead of half-applying).
"""
import argparse
import glob
import io
import os
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFilter

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "Tools" / "VulkanShaders" / "bls"))
sys.path.insert(0, r"R:\Users\Zach\Documents\GitHub\EsteriaWoW\tools")
import mpq_extract  # noqa: E402
import cars_mount_pack  # noqa: E402

B = chr(92)
DUI = "Interface" + B + "AddOns" + B + "DragonUI" + B
STORM_DLL = pathlib.Path(r"R:\Users\Zach\Downloads\battlemon\Client\wdbx-2.4.1.a-extended-dbc\x64\StormLib.dll")

# (file, old, new, expected matches)
PATCHES = {
    "modules" + B + "modules.xml": [
        ('<Script file="micromenu.lua"/>', '<Script file="esteria_microbuttons.lua"/>\n    <Script file="micromenu.lua"/>', 1),
    ],
    "modules" + B + "micromenu.lua": [
        # the strip: Esteria's three buttons sit after Collections
        ('MicroButtonFor("MainMenu"), (MicroButtonFor("Help")),',
         'MicroButtonFor("Transmog"), MicroButtonFor("Flight"), MicroButtonFor("Radio"),\n    MicroButtonFor("MainMenu"), (MicroButtonFor("Help")),', 1),
        # three more buttons of 26 px each: the strip starts further left
        ("-180", "-258", 3),
        # one row, however many buttons the profile's saved column count allows
        ("local columns = math.floor(tonumber(config.columns) or 12)", "local columns = math.floor(math.max(tonumber(config.columns) or 12, 14))", 1),
        # atlas rows for the new sheets
        ("-- The fifth field names the file: DragonUI's colour sheet predates the professions icon.",
         '''for _, spec in pairs(addon.EsteriaMicro or {}) do
    for i, state in ipairs({ "Up", "Mouseover", "Down", "Disabled" }) do
        MicromenuAtlas["UI-HUD-MicroMenu-" .. spec.atlas .. "-" .. state] = { (i - 1) * 0.25, i * 0.25, 0, 0.640625, spec.sheet }
    end
end
-- The fifth field names the file: DragonUI's colour sheet predates the professions icon.''', 1),
        ("    return buttonMap[buttonName]\nend",
         '''    local esteria = addon.EsteriaMicro and addon.EsteriaMicro[buttonName]
    return buttonMap[buttonName] or (esteria and "UI-HUD-MicroMenu-" .. esteria.atlas)
end''', 1),
        # Esteria's art is colour only: the client's grayscale atlas has no rows for it
        ("local shouldUseGrayscale = useGrayscale or (not isPVPButton and not upCoords and not isCharacterButton)",
         "local shouldUseGrayscale = not (addon.EsteriaMicro and addon.EsteriaMicro[name])\n                and (useGrayscale or (not isPVPButton and not upCoords and not isCharacterButton))", 1),
    ],
    "modules" + B + "talents" + B + "entry_points.lua": [
        # Character Advancement replaces the talent window; esteria_microbuttons.lua owns the Talents button
        ('if TM.applied or not IsLoggedIn() or not addon:IsModuleEnabled("talents") then return end',
         "if true then return end -- Esteria: the DragonUI talent window is off, Character Advancement replaces it", 1),
    ],
}


def patched_dragonui(client):
    archive = glob.glob(os.path.join(client, "Data", "*DragonUI.MPQ"))[0]
    mpq = mpq_extract.MPQ(archive)
    out = {}
    for name, edits in PATCHES.items():
        text = mpq.read_file(DUI + name)[0].decode("utf-8")
        for old, new, count in edits:
            if text.count(old) != count:
                raise SystemExit(f"{name}: expected {count} match(es) of {old[:60]!r}, found {text.count(old)}")
            text = text.replace(old, new)
        out[DUI + name] = text.encode("utf-8")
    return out


def sheet(draw_icon):
    """Four 64x82 cells (up, mouseover, down, disabled) on a 256x128 sheet, drawn at 4x and reduced."""
    S = 4
    big = Image.new("RGBA", (256 * S, 128 * S), (0, 0, 0, 0))
    for i, (tint, dx, glow) in enumerate([((235, 200, 110), 0, 0), ((255, 235, 160), 0, 1), ((190, 150, 70), 1, 0), ((120, 120, 120), 0, 0)]):
        cell = Image.new("RGBA", (64 * S, 82 * S), (0, 0, 0, 0))
        if glow:
            halo = Image.new("RGBA", cell.size, (0, 0, 0, 0))
            draw_icon(ImageDraw.Draw(halo), S, (255, 230, 150))
            cell = Image.alpha_composite(cell, halo.filter(ImageFilter.GaussianBlur(5 * S)))
        draw_icon(ImageDraw.Draw(cell), S, tint)
        big.alpha_composite(cell, (i * 64 * S + dx * S, dx * S))
    return big.resize((256, 128), Image.LANCZOS)


def tunic(d, S, c):
    # a chest piece with a sparkle: the transmog mark
    cx, cy = 32 * S, 41 * S
    pts = [(-17, -14), (-8, -17), (-4, -12), (4, -12), (8, -17), (17, -14), (21, -2), (13, 1), (11, 18), (-11, 18), (-13, 1), (-21, -2)]
    d.polygon([(cx + x * S, cy + y * S) for x, y in pts], fill=c + (255,), outline=(60, 40, 10, 255))
    d.line([(cx, cy - 12 * S), (cx, cy + 18 * S)], fill=(60, 40, 10, 200), width=S)
    sx, sy, r = cx + 15 * S, cy - 17 * S, 7 * S
    d.polygon([(sx, sy - r), (sx + r // 3, sy - r // 3), (sx + r, sy), (sx + r // 3, sy + r // 3), (sx, sy + r), (sx - r // 3, sy + r // 3), (sx - r, sy), (sx - r // 3, sy - r // 3)], fill=(255, 255, 255, 255))


def wing(d, S, c):
    import math
    ox, oy = 32 * S - 14 * S, 41 * S + 16 * S
    for k, (ang, length) in enumerate([(-82, 32), (-66, 36), (-50, 37), (-34, 35), (-18, 30), (-4, 23)]):
        a, w = math.radians(ang), math.radians(ang + 90)
        tip = (ox + math.cos(a) * length * S, oy + math.sin(a) * length * S)
        mid = (ox + math.cos(a) * length * 0.55 * S, oy + math.sin(a) * length * 0.55 * S)
        wx, wy = math.cos(w) * 4.5 * S, math.sin(w) * 4.5 * S
        d.polygon([(ox, oy), (mid[0] + wx, mid[1] + wy), tip, (mid[0] - wx, mid[1] - wy)], fill=c + (255,), outline=(60, 40, 10, 255))
    d.ellipse([ox - 5 * S, oy - 5 * S, ox + 5 * S, oy + 5 * S], fill=c + (255,), outline=(60, 40, 10, 255))


def tga(img):
    buf = io.BytesIO()
    img.save(buf, "TGA")
    return buf.getvalue()


def tree(root, prefix):
    """Every file under root as {archive name: bytes}."""
    out = {}
    for path in root.rglob("*"):
        if path.is_file():
            out[prefix + str(path.relative_to(root)).replace("/", B).replace(os.sep, B)] = path.read_bytes()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--client", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    entries = patched_dragonui(args.client)
    entries[DUI + "modules" + B + "esteria_microbuttons.lua"] = (REPO / "Addons" / "EsteriaShell" / "esteria_microbuttons.lua").read_bytes()
    entries[DUI + "Textures" + B + "Micromenu" + B + "esteria-transmog.tga"] = tga(sheet(tunic))
    entries[DUI + "Textures" + B + "Micromenu" + B + "esteria-flight.tga"] = tga(sheet(wing))
    for addon in ("EverlookBroadcasting", "Manastorm_Transmog"):
        entries.update(tree(REPO / "Addons" / addon, "Interface" + B + "AddOns" + B + addon + B))

    out = pathlib.Path(args.out)
    if out.exists():
        out.unlink()
    cars_mount_pack.Storm(STORM_DLL).create_archive(out, entries)
    print(f"{out}: {len(entries)} files, {out.stat().st_size} bytes")


if __name__ == "__main__":
    main()
