"""Builds Patch-ZzzzzzzzzzzEsteriaShell.MPQ: the Esteria additions to the DragonUI micro strip, the Everlook radio
add-on, the transmog window and the character swap wheel with its sky and the Paths of Fate Codex, as one archive that sorts after every other
patch.

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
STORM_DLL = REPO / "Tools" / "MountStudio" / "runtime" / "backend" / "StormLib.dll"

# (file, old, new, expected matches)
PATCHES = {
    "modules" + B + "modules.xml": [
        ('<Script file="micromenu.lua"/>', '<Script file="esteria_microbuttons.lua"/>\n    <Script file="micromenu.lua"/>', 1),
    ],
    "modules" + B + "micromenu.lua": [
        # the strip: Esteria's five buttons sit after Collections
        ('MicroButtonFor("MainMenu"), (MicroButtonFor("Help")),',
         'MicroButtonFor("Transmog"), MicroButtonFor("Flight"), MicroButtonFor("Fate"), MicroButtonFor("Radio"), MicroButtonFor("Neighborhood"),\n    MicroButtonFor("MainMenu"), (MicroButtonFor("Help")),', 1),
        # five more buttons of 26 px each: the strip starts further left
        ("-180", "-310", 3),
        # one row, however many buttons the profile's saved column count allows
        ("local columns = math.floor(tonumber(config.columns) or 12)", "local columns = math.floor(math.max(tonumber(config.columns) or 12, 16))", 1),
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


def compass(d, S, c):
    # a four-pointed star in a ring: the Paths of Fate mark
    cx, cy = 32 * S, 41 * S
    d.ellipse([cx - 19 * S, cy - 19 * S, cx + 19 * S, cy + 19 * S], outline=c + (255,), width=3 * S)
    for dx, dy in ((0, -1), (1, 0), (0, 1), (-1, 0)):
        tip = (cx + dx * 24 * S, cy + dy * 24 * S)
        side = (-dy * 6 * S, dx * 6 * S)
        d.polygon([tip, (cx + side[0], cy + side[1]), (cx, cy), (cx - side[0], cy - side[1])],
                  fill=c + (255,), outline=(60, 40, 10, 255))
    d.ellipse([cx - 4 * S, cy - 4 * S, cx + 4 * S, cy + 4 * S], fill=(255, 255, 255, 255))


def house_at(d, cx, cy, u, c):
    # a gabled house with a door: the Neighborhood mark, about 44 u wide
    edge = (60, 40, 10, 255)
    d.polygon([(cx - 22 * u, cy - 2 * u), (cx, cy - 22 * u), (cx + 22 * u, cy - 2 * u)], fill=c + (255,), outline=edge)
    d.rectangle([cx - 15 * u, cy - 2 * u, cx + 15 * u, cy + 20 * u], fill=c + (255,), outline=edge)
    d.rectangle([cx - 5 * u, cy + 6 * u, cx + 5 * u, cy + 20 * u], fill=edge)


def house(d, S, c):
    house_at(d, 32 * S, 41 * S, S, c)


def stock_micro(tint, dx=0):
    """One state of the stock micro bar's button: a 32x64 texture with the frame in its lower part, as Blizzard's are."""
    S = 4
    big = Image.new("RGBA", (32 * S, 64 * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    d.rounded_rectangle([(3 + dx) * S, (24 + dx) * S, (28 + dx) * S, (60 + dx) * S], radius=3 * S, fill=(42, 26, 18, 255), outline=tint + (255,), width=S)
    house_at(d, (16 + dx) * S, (43 + dx) * S, S * 0.45, tint)
    return big.resize((32, 64), Image.LANCZOS)


def cloud_noise(width, height, seed, octaves=6):
    """Fractal value noise in [0, 1], height x width."""
    import numpy as np
    rng = np.random.default_rng(seed)
    noise = np.zeros((height, width))
    for octave in range(octaves):
        cells = 4 * 2 ** octave
        grid = Image.fromarray((rng.random((cells * height // width + 2, cells + 1)) * 255).astype(np.uint8))
        noise += np.asarray(grid.resize((width, height), Image.BICUBIC), dtype=float) / 255 / 2 ** octave
    noise -= noise.min()
    return noise / noise.max()


def swap_sky(width=2048, height=512):
    """The character swap's sky (client cover and in-world curtain), as two layers each shown half its width wide:
    a blue gradient under soft clouds, and nearer wisps with alpha that drift faster in front of it."""
    import numpy as np
    y = np.linspace(0.0, 1.0, height)[:, None]
    noise = cloud_noise(width, height, 12340)
    # denser clouds lower down, as if looking out across the cloud tops
    cover = np.clip((noise - 0.62 + 0.35 * y) * 3.0, 0.0, 1.0)
    top, horizon = np.array([58, 112, 178]), np.array([170, 205, 235])
    sky = top + (horizon - top) * y[..., None]
    cloud = np.array([246, 248, 252]) - 40 * (1 - noise[..., None])
    back = sky * (1 - cover[..., None]) + cloud * cover[..., None]
    back = Image.fromarray(back.clip(0, 255).astype(np.uint8), "RGB").filter(ImageFilter.GaussianBlur(1.5))

    wisps = cloud_noise(width, height, 33580, octaves=5)
    alpha = np.clip((wisps - 0.55) * 2.2, 0.0, 1.0) * 0.85 * (0.4 + 0.6 * y)
    shade = (235 + 20 * wisps).clip(0, 255)
    front = np.dstack([shade, shade, shade + 4, alpha * 255]).clip(0, 255).astype(np.uint8)
    front = Image.fromarray(front, "RGBA").filter(ImageFilter.GaussianBlur(2.0))
    return back, front


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
    entries[DUI + "Textures" + B + "Micromenu" + B + "esteria-fate.tga"] = tga(sheet(compass))
    entries[DUI + "Textures" + B + "Micromenu" + B + "esteria-neighborhood.tga"] = tga(sheet(house))
    # the stock micro bar's Neighborhood button (Addons/EsteriaHousing/Neighborhood.lua), for when DragonUI is off
    for state, tint, dx in (("Up", (235, 200, 110), 0), ("Down", (190, 150, 70), 1), ("Disabled", (120, 120, 120), 0)):
        entries["Interface" + B + "Esteria" + B + "NeighborhoodMicro-" + state + ".tga"] = tga(stock_micro(tint, dx))
    back, front = swap_sky()
    entries["Interface" + B + "Esteria" + B + "SwapSkyBack.tga"] = tga(back)
    entries["Interface" + B + "Esteria" + B + "SwapSkyFront.tga"] = tga(front)
    for addon in ("EverlookBroadcasting", "Manastorm_Transmog", "EsteriaSwap", "EsteriaPerks"):
        entries.update(tree(REPO / "Addons" / addon, "Interface" + B + "AddOns" + B + addon + B))
    # The Codex draws with the character creator's style kit: one file, shipped in both places.
    entries["Interface" + B + "AddOns" + B + "EsteriaPerks" + B + "esteria_style.lua"] = (
        REPO / "Tools" / "RetailAssets" / "glue" / "esteria_style.lua").read_bytes()
    # The swap wheel's art (EasyController, MIT: easycontroller/README.md)
    for png in (REPO / "Tools" / "EsteriaShell" / "easycontroller").glob("*.png"):
        name = "Interface" + B + "AddOns" + B + "EsteriaSwap" + B + "Textures" + B + png.stem + ".tga"
        entries[name] = tga(Image.open(png).convert("RGBA"))

    out = pathlib.Path(args.out)
    if out.exists():
        out.unlink()
    cars_mount_pack.Storm(STORM_DLL).create_archive(out, entries)
    print(f"{out}: {len(entries)} files, {out.stat().st_size} bytes")


if __name__ == "__main__":
    main()
