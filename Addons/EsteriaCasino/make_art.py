"""Builds the casino window's texture atlases as BLP files in Art/.

  python make_art.py [source_root]

source_root defaults to G:\\Downloads\\HDWoWModels\\BoardGames. The packs' licences forbid redistributing the art,
and this repo is public, so Art/ is gitignored: build it locally and ship it only inside the client MPQ.
Credits: "Casino Table UI Kit by MCreative", "PARLOUR by najjar320" (font: Alfa Slab One, SIL OFL).

Atlases (cell layouts must match EsteriaCasino.lua):
  table.blp      1024x512   green felt + the blackjack arc layout, cut down to our one-seat, no-insurance table
  cards.blp      2048x1024  128x180 cells; rows clubs, diamonds, hearts, spades; columns A 2..10 J Q K; row 4: backs
  chips.blp      512x256    128x128 cells: 1, 5, 10, 25 / 100, 500, 1K, 5K
  buttons.blp    1024x2048  256x128 cells; rows BUTTONS below; columns Normal, Pressed, Off
  slots.blp      1024x512   studded leather behind the reels
  dicetable.blp  1024x512   red felt behind the dice
  dice.blp       1024x128   128x128 ivory faces 1..6
  cup.blp        1024x512   128x128 cells, 8 per row: the 24 shake frames of the dice cup (before it lifts)
  title_*.blp    1024x512   256x64 cells, 4 per row: 19 frames of an animated gold title, evenly picked
  fx_*.blp       1024x512   128x128 cells, 8 per row: 24 frames of a gold effect, evenly picked
  pieces.blp     1024x256   128x128 cells: white P N B R Q K, black P N B R Q K, checkers white, white king,
                            black, black king
  balls.blp      1024x512   128x128 cells, 8 per row: bingo balls 1..20
  chessbg.blp, checkersbg.blp, poker.blp, lottery.blp   1024x512 backdrops (the window stretches them to 16:9)
"""
import math
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "Tools" / "CosmeticsPack"))
from backpack_icons import blp_bytes  # noqa: E402  proven BLP2 BGRA writer for this client

SOURCE = Path(sys.argv[1] if len(sys.argv) > 1 else r"G:\Downloads\HDWoWModels\BoardGames")
KIT = SOURCE / "casino-table-ui-kit"
CLASSIC = SOURCE / "parlour-board-game-assets-v1.0" / "classic"
PARLOUR = CLASSIC / "02_game-pieces" / "cards"
FONT = SOURCE / "parlour-board-game-assets-v1.0" / "_source" / "fonts" / "AlfaSlabOne-Regular.ttf"
OUT = HERE / "Art"

CARD_W, CARD_H = 128, 180
SUITS = ["clubs", "diamonds", "hearts", "spades"]
RANKS = ["A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"]
BACKS = ["classic_card_back-01_lattice-red", "classic_card_back-12_crest-green"]
CHIPS = ["1", "5", "10", "25", "100", "500", "1K", "5K"]
BUTTONS = ["HIT", "STAND", "DOUBLE", "SPLIT", "DEAL", "CLEAR", "REBET", "MAX_BET", "SPIN", "Blank"]
STATES = ["Normal", "Pressed", "Off"]
CUP_FRAMES = 24
TITLE_FRAMES, FX_FRAMES = 19, 24
# animated gold title -> (sheet columns, frames in the sheet); from catalogue.csv
TITLES = {"you-win": (6, 37), "blackjack": (6, 40), "jackpot": (6, 37), "lucky-roll": (6, 41), "draw": (6, 30),
          "checkmate": (6, 40), "game-over": (6, 40), "you-lose": (6, 38)}
CHESS = ["pawn", "knight", "bishop", "rook", "queen", "king"]
CHECKERS = ["white", "white-king", "black", "black-king"]
EFFECTS = {"coin-burst": (8, 30), "confetti": (8, 30), "fireworks-loop": (8, 30), "glow-ring-pulse": (8, 24)}

# Blackjack layout geometry, from Layouts/SVG/Layout_Blackjack_Arc.svg (2048x1152 space).
ARC_CENTRE = (1024, -40)          # both text arcs (r 650 and 745) share it
INSURANCE_BAND = (655, 740)       # radii between the arcs: "INSURANCE PAYS 2 TO 1"
SEAT = (1024, 885)                # the one betting circle we keep
SIDE_SEATS = [(305.14, 542.12), (506.75, 726.86), (753.56, 844.58), (1294.44, 844.58), (1541.25, 726.86),
              (1742.86, 542.12)]
INK = (242, 217, 142)             # the layout's #f2d98e


def fit(image, size):
    """Scales the image to fit inside size, centred on a transparent cell."""
    image = image.convert("RGBA")
    scale = min(size[0] / image.width, size[1] / image.height)
    scaled = image.resize((max(1, round(image.width * scale)), max(1, round(image.height * scale))),
                          Image.Resampling.LANCZOS)
    cell = Image.new("RGBA", size, (0, 0, 0, 0))
    cell.alpha_composite(scaled, ((size[0] - scaled.width) // 2, (size[1] - scaled.height) // 2))
    return cell


def save(image, name):
    with tempfile.TemporaryDirectory() as tmp:
        png = Path(tmp) / "atlas.png"
        image.save(png)
        (OUT / f"{name}.blp").write_bytes(blp_bytes(png))
    print(f"wrote Art/{name}.blp {image.size}")


def arc_text(layer, text, radius, size, alpha):
    """Writes text along the bottom of a circle around ARC_CENTRE, upright and centred straight below it."""
    font = ImageFont.truetype(str(FONT), size)
    widths = [font.getlength(ch) for ch in text]
    span = sum(widths) / radius
    theta = math.pi / 2 + span / 2  # start on the left (y grows downward)
    for ch, width in zip(text, widths):
        mid = theta - width / radius / 2
        glyph = Image.new("RGBA", (size * 2, size * 2), (0, 0, 0, 0))
        ImageDraw.Draw(glyph).text((size, size), ch, font=font, fill=INK + (alpha,), anchor="mm")
        glyph = glyph.rotate(math.degrees(math.pi / 2 - mid), resample=Image.Resampling.BICUBIC)
        x = ARC_CENTRE[0] + radius * math.cos(mid) - size
        y = ARC_CENTRE[1] + radius * math.sin(mid) - size
        layer.alpha_composite(glyph, (round(x), round(y)))
        theta -= width / radius


def table():
    felt = Image.open(KIT / "Felts" / "Felt_Green.png").convert("RGBA")
    layout = Image.open(KIT / "Layouts" / "PNG" / "Layout_Blackjack_Arc.png").convert("RGBA")

    # We seat one player and offer no insurance: clear the six side seats and the insurance line.
    mask = Image.new("L", layout.size, 255)
    draw = ImageDraw.Draw(mask)
    inner, outer = INSURANCE_BAND
    cx, cy = ARC_CENTRE
    draw.ellipse((cx - outer, cy - outer, cx + outer, cy + outer), fill=0)  # clear the band...
    draw.ellipse((cx - inner, cy - inner, cx + inner, cy + inner), fill=255)  # ...but keep what's inside it
    for x, y in SIDE_SEATS:
        draw.ellipse((x - 88, y - 88, x + 88, y + 88), fill=0)
    layout.putalpha(Image.composite(layout.getchannel("A"), Image.new("L", layout.size, 0), mask))
    arc_text(layout, "ESTERIA CASINO", (inner + outer) / 2, 54, 225)

    board = Image.new("RGBA", layout.size)
    for x in range(0, layout.width, felt.width):
        for y in range(0, layout.height, felt.height):
            board.alpha_composite(felt, (x, y))
    board.alpha_composite(layout)
    # Squashed to a power of two here; the window stretches it back to 16:9.
    save(board.resize((1024, 512), Image.Resampling.LANCZOS), "table")


def backdrop(name, out):
    image = Image.open(CLASSIC / "01_backgrounds" / "card-tables" / f"classic_bg_{name}_1920x1080.png")
    save(image.convert("RGBA").resize((1024, 512), Image.Resampling.LANCZOS), out)


def cards():
    atlas = Image.new("RGBA", (2048, 1024), (0, 0, 0, 0))
    for row, suit in enumerate(SUITS):
        for col, rank in enumerate(RANKS):
            card = Image.open(PARLOUR / "ivory" / f"classic_card_ivory_{suit}-{rank}_750x1050.png")
            atlas.alpha_composite(fit(card, (CARD_W, CARD_H)), (col * CARD_W, row * CARD_H))
    for col, back in enumerate(BACKS):
        card = Image.open(PARLOUR / "backs" / f"{back}_750x1050.png")
        atlas.alpha_composite(fit(card, (CARD_W, CARD_H)), (col * CARD_W, 4 * CARD_H))
    save(atlas, "cards")


def chips():
    atlas = Image.new("RGBA", (512, 256), (0, 0, 0, 0))
    for i, value in enumerate(CHIPS):
        chip = Image.open(KIT / "Chips" / "PNG-1x" / f"Chip_{value}_Top.png")
        atlas.alpha_composite(fit(chip, (128, 128)), ((i % 4) * 128, (i // 4) * 128))
    save(atlas, "chips")


def buttons():
    atlas = Image.new("RGBA", (1024, 2048), (0, 0, 0, 0))
    for row, label in enumerate(BUTTONS):
        for col, state in enumerate(STATES):
            button = Image.open(KIT / "Buttons" / "PNG-1x" / f"Button_{label}_{state}.png")
            atlas.alpha_composite(fit(button, (256, 128)), (col * 256, row * 128))
    save(atlas, "buttons")


def dice():
    atlas = Image.new("RGBA", (1024, 128), (0, 0, 0, 0))
    for face in range(1, 7):
        die = Image.open(CLASSIC / "02_game-pieces" / "dice" / f"classic_dice_3d_ivory-{face}_300.png")
        atlas.alpha_composite(fit(die, (128, 128)), ((face - 1) * 128, 0))
    save(atlas, "dice")

    sheet = Image.open(CLASSIC / "03_gameplay-animations" / "dice" /
                       "classic_anim_dice-cup-shake-reveal_sheet_8x5_512.png").convert("RGBA")
    cup = Image.new("RGBA", (1024, 512), (0, 0, 0, 0))
    for i in range(CUP_FRAMES):
        frame = sheet.crop(((i % 8) * 512, (i // 8) * 512, (i % 8 + 1) * 512, (i // 8 + 1) * 512))
        cup.alpha_composite(frame.resize((128, 128), Image.Resampling.LANCZOS), ((i % 8) * 128, (i // 8) * 128))
    save(cup, "cup")


def frames_from(sheet, cols, size, total, wanted):
    """Picks `wanted` frames evenly (first and last included) from a COLSxROWS sheet of size-sized cells."""
    for i in range(wanted):
        n = round(i * (total - 1) / (wanted - 1))
        yield sheet.crop(((n % cols) * size[0], (n // cols) * size[1], (n % cols + 1) * size[0],
                          (n // cols + 1) * size[1]))


def animations():
    for name, (cols, total) in TITLES.items():
        path = next((CLASSIC / "04_animated-titles" / "gold").glob(f"classic_title_{name}_sheet_*_960x240.png"))
        atlas = Image.new("RGBA", (1024, 512), (0, 0, 0, 0))
        for i, frame in enumerate(frames_from(Image.open(path).convert("RGBA"), cols, (960, 240), total, TITLE_FRAMES)):
            atlas.alpha_composite(frame.resize((256, 64), Image.Resampling.LANCZOS), ((i % 4) * 256, (i // 4) * 64))
        save(atlas, "title_" + name.replace("-", ""))
    for name, (cols, total) in EFFECTS.items():
        path = next((CLASSIC / "05_effects").glob(f"classic_fx_{name}_gold_sheet_*_512.png"))
        atlas = Image.new("RGBA", (1024, 512), (0, 0, 0, 0))
        for i, frame in enumerate(frames_from(Image.open(path).convert("RGBA"), cols, (512, 512), total, FX_FRAMES)):
            atlas.alpha_composite(frame.resize((128, 128), Image.Resampling.LANCZOS), ((i % 8) * 128, (i // 8) * 128))
        save(atlas, "fx_" + name.split("-")[0])


def board_games():
    atlas = Image.new("RGBA", (1024, 256), (0, 0, 0, 0))
    for colour_index, colour in enumerate(["white", "black"]):
        for i, piece in enumerate(CHESS):
            image = Image.open(CLASSIC / "02_game-pieces" / "chess" / f"classic_chess_{colour}-{piece}_300x400.png")
            cell = colour_index * 6 + i
            atlas.alpha_composite(fit(image, (120, 120)), ((cell % 8) * 128 + 4, (cell // 8) * 128 + 4))
    for i, piece in enumerate(CHECKERS):
        image = Image.open(CLASSIC / "02_game-pieces" / "checkers" / f"classic_checkers_{piece}_256.png")
        cell = 12 + i
        atlas.alpha_composite(fit(image, (116, 116)), ((cell % 8) * 128 + 6, (cell // 8) * 128 + 6))
    save(atlas, "pieces")

    balls = Image.new("RGBA", (1024, 512), (0, 0, 0, 0))
    for n in range(1, 21):
        image = Image.open(CLASSIC / "02_game-pieces" / "bingo-balls" / f"classic_bingo_{n:02d}_256.png")
        balls.alpha_composite(fit(image, (128, 128)), (((n - 1) % 8) * 128, ((n - 1) // 8) * 128))
    save(balls, "balls")

    backdrops = (("chess-boards", "chess-wood", "chessbg"), ("chess-boards", "checkers-red-black", "checkersbg"),
                 ("card-tables", "poker-table-oval", "poker"), ("card-tables", "felt-blue", "lottery"))
    for folder, name, out in backdrops:
        image = Image.open(CLASSIC / "01_backgrounds" / folder / f"classic_bg_{name}_1920x1080.png")
        save(image.convert("RGBA").resize((1024, 512), Image.Resampling.LANCZOS), out)


def main():
    OUT.mkdir(exist_ok=True)
    table()
    cards()
    chips()
    buttons()
    backdrop("leather-studded", "slots")
    backdrop("felt-red", "dicetable")
    dice()
    animations()
    board_games()


if __name__ == "__main__":
    main()
