"""Arcade content: the test ROM set -> client patch MPQ, libretro cores -> client folder, and the server's game,
cabinet and spawn SQL.

    python pack.py --cores <dir with *_libretro.dll> --out <new dir> [--install]

--install copies Patch-ZzzzzzzzzzzEsteriaArcadeRoms.MPQ into the Dev client's Data, the cores into <client>\\Arcade\\Cores
(backing up anything replaced) and applies the SQL schema + content to housing_dev_world / housing_dev_characters.
The ROMs are the owner's own dumps for testing; only the files listed in GAMES are packed.
"""
import argparse
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CLIENT = Path("G:/3.3.5a - Dev")
ARCHIVE = "Patch-ZzzzzzzzzzzEsteriaArcadeRoms.MPQ"
MODULE = REPO.parent / "EsteriaWoW/modules/mod-esteria-arcade"
CORES = {"nes": "fceumm", "snes": "snes9x", "genesis": "genesis_plus_gx", "gb": "gambatte", "gbc": "gambatte", "gba": "mgba"}
V = Path("V:/ROMs")

# id, title, system, source file. Cabinet entry = 9610010 + id; ids are stable (saves are keyed by them).
GAMES = [
    (1, "Kirby's Adventure", "nes", V / "NES/Kirby's Adventure.nes"),
    (2, "Super Mario Bros. & Duck Hunt", "nes", V / "NES/3-in-1 Super Mario Bros - Duck Hunt - World Class Track Meet.nes"),
    (3, "Dr. Mario", "nes", V / "NES/Dr. Mario.nes"),
    (4, "Contra III: The Alien Wars", "snes", V / "SNES/Contra III - The Alien Wars.smc"),
    (5, "ActRaiser", "snes", V / "SNES/ActRaiser.smc"),
    (6, "Tetris & Dr. Mario", "snes", V / "SNES/Tetris & Dr. Mario.smc"),
    (7, "Sonic the Hedgehog 2", "genesis", V / "SEGA/S/Sonic The Hedgehog 2 (W) [!].bin"),
    (8, "Streets of Rage 2", "genesis", V / "SEGA/S/Streets of Rage 2 (U) [!].bin"),
    (9, "Phantasy Star IV", "genesis", V / "SEGA/P/Phantasy Star - The End of the Millenium (U) [!].bin"),
    (10, "Pokemon Blue", "gb", V / "Nintendo GameBoy/Pokemon Blue.gb"),
    (11, "Pokemon Trading Card Game", "gb", V / "Nintendo GameBoy/Pokemon Trading Card Game.gb"),
    (12, "Link's Awakening DX", "gbc", V / "Nintendo GameBoy Color/Legend of Zelda, The - Link's Awakening DX (USA, Europe).gbc"),
    (13, "Pokemon Crystal", "gbc", V / "Nintendo GameBoy Color/Pokemon - Crystal Version (USA, Europe).gbc"),
    (14, "Pokemon Emerald", "gba", V / "Nintendo GameBoy Advance/Pokemon - Emerald Version (USA, Europe).gba"),
    (15, "Dragon Ball Z: The Legacy of Goku II", "gba", V / "Nintendo GameBoy Advance/1085 - Dragon Ball Z - The Legacy of Goku II (U)(TrashMan).gba"),
]
CORES.update({"n64": "parallel_n64", "arcade": "fbneo", "ps1": "pcsx_rearmed",
              "dos": "dosbox_pure",  # DOS games: a zip with the game installed (dos_warcraft.py)
              "pc": "starcraft"})    # Windows games run hidden through Tools/ArcadeCores/starcraft's bridge
# A PC game ships its staged folder; the first file is the one the core starts (the catalog row's file). StarCraft's
# is listed (no player profiles, logs or unpacked maps: the core unpacks maps.zip itself); any other ships the whole
# folder of its exe, minus PC_SKIP.
PC_FILES = {"StarCraft: Brood War": ["StarCraft.exe", "storm.dll", "Smackw32.dll", "Local.dll", "standard.snp", "battle.snp",
                                     "StarDat.mpq", "BrooDat.mpq", "patch_rt.mpq", "StarCraft.mpq", "BroodWar.mpq",
                                     "ddraw.dll", "maps.zip"]}
PC_SKIP = {"esteria_bridge.log", "_saves", "warcraftdocs", "debug.ini", "autorun.inf", "service menu.jpg"}  # lower case


def pc_files(exe):
    """The exe first, then every other file under its folder (subfolders kept), minus PC_SKIP names."""
    root = exe.parent
    rest = sorted(f for f in root.rglob("*") if f.is_file() and f != exe
                  and not any(part.lower() in PC_SKIP for part in f.relative_to(root).parts))
    return [exe.name] + [str(f.relative_to(root)) for f in rest]  # Windows separators, as archive paths need


# A game with a cabinet model of its own (Tools/ArcadeProp PROPS): (display id, housing model_fdid). Others: Lionheart.
GAME_DISPLAYS = {"StarCraft: Brood War": (3610014, 0x40000008), "WarCraft: Orcs & Humans": (3610015, 0x40000009),
                 "Warcraft II: Battle.net Edition": (3610016, 0x4000000A), "The Lost Vikings": (3610017, 0x4000000B),
                 "Rock n' Roll Racing": (3610018, 0x4000000C),
                 "Warcraft Adventures: Lord of the Clans": (3610019, 0x4000000D)}
# The owner's master list (2026-11-22), resolved by resolve_catalog.py. New games get permanent ids here (written back
# into catalog.json); games already in GAMES keep theirs. Each system ships in its own archive.
CATALOG = Path(__file__).with_name("catalog.json")
SYSTEM_ARCHIVE = "Patch-ZzzzzzzzzzzEsteriaArcadeRoms-{}.MPQ"
MAME_TITLES = {
    "1942": "1942", "asteroid": "Asteroids", "avsp": "Alien vs. Predator", "centiped": "Centipede", "defender": "Defender",
    "digdug": "Dig Dug", "dino": "Cadillacs and Dinosaurs", "dkong": "Donkey Kong", "ffight": "Final Fight (Arcade)",
    "frogger": "Frogger", "galaga": "Galaga", "gauntlet": "Gauntlet", "goldnaxe": "Golden Axe",
    "invaders": "Space Invaders (Arcade)", "kof2002": "The King of Fighters 2002", "kof98": "The King of Fighters '98",
    "mslug": "Metal Slug", "mslug2": "Metal Slug 2", "mslug3": "Metal Slug 3", "mslugx": "Metal Slug X",
    "mspacman": "Ms. Pac-Man (Arcade)", "mvsc": "Marvel vs. Capcom", "pacman": "Pac-Man", "punisher": "The Punisher",
    "rampage": "Rampage", "sf2": "Street Fighter II (Arcade)", "sfa3": "Street Fighter Alpha 3",
    "sfiii3n": "Street Fighter III: 3rd Strike", "simpsons": "The Simpsons", "tmnt": "Teenage Mutant Ninja Turtles",
    "tmnt2": "TMNT: Turtles in Time", "umk3": "Ultimate Mortal Kombat 3",
}
PRICE = 5000             # 50 silver per session at public cabinets
DISPLAY, BASE_ENTRY, FIRST_CABINET = 3610001, 9610010, 9610001
DECOR_BASE = 3610000     # housing_decor_catalog.decor_id = DECOR_BASE + game id
SHOWCASE = [1, 4, 7, 10, 12, 14]  # one cabinet per system spawned beside the original one


def slug(title):
    return "".join(c if c.isalnum() else "_" for c in title).strip("_")


def rom_path(game):
    gid, title, system, source = game
    return f"Arcade\\Roms\\{system.upper()}\\{slug(title)}{source.suffix.lower()}"


def cue_tracks(cue):
    """The track files a .cue sheet names (they must sit beside it)."""
    tracks = []
    for line in cue.read_text(errors="replace").splitlines():
        line = line.strip()
        if line.upper().startswith("FILE") and '"' in line:
            tracks.append(line.split('"')[1])
    return tracks


def all_games():
    """Every game as {id, title, system, files: [(archive path, source)]}: GAMES, then the catalog's new ones."""
    games = [{"id": g[0], "title": g[1], "system": g[2], "files": [(rom_path(g), g[3])], "base": True} for g in GAMES]
    rows = json.loads(CATALOG.read_text())
    known = {hashlib.sha256(g[3].read_bytes()).hexdigest() for g in GAMES}
    next_id = max([g[0] for g in GAMES] + [r.get("id", 0) for r in rows]) + 1
    changed = False
    for r in rows:
        if r["sha256"] in known or r.get("skip"):
            continue
        if "id" not in r:
            r["id"], next_id, changed = next_id, next_id + 1, True
        src, system = Path(r["file"]), r["system"]
        title = MAME_TITLES.get(src.stem, src.stem) if system == "arcade" else r["title"]
        if system == "arcade":  # FBNeo knows a set by its zip name; Neo Geo sets need the BIOS beside them
            files = [(f"Arcade\\Roms\\ARCADE\\{src.name}", src), ("Arcade\\Roms\\ARCADE\\neogeo.zip", src.with_name("neogeo.zip"))]
        elif system == "pc":
            folder = f"Arcade\\Roms\\PC\\{slug(r['title'])}"
            names = PC_FILES.get(r["title"]) or pc_files(src)
            files = [(f"{folder}\\{name}", src.parent / name) for name in names]
        elif system == "ps1":
            folder = f"Arcade\\Roms\\PS1\\{slug(title)}"
            files = [(f"{folder}\\{src.name}", src)] + [(f"{folder}\\{t}", src.with_name(t)) for t in cue_tracks(src)]
        else:
            name, n = slug(title), 1
            taken = {f[0].lower() for g in games for f in g["files"][:1]}
            while f"arcade\\roms\\{system}\\{name}{src.suffix}".lower() in taken:  # a second dump of the same game
                n += 1
                name, title = f"{slug(r['title'])}_{n}", f"{r['title']} ({n})"
            files = [(f"Arcade\\Roms\\{system.upper()}\\{name}{src.suffix.lower()}", src)]
        games.append({"id": r["id"], "title": title, "system": system, "files": files, "base": False})
    if changed:
        CATALOG.write_text(json.dumps(rows, indent=1) + "\n")
    paths = [f[0].lower() for g in games for f in g["files"][:1]]
    assert len(paths) == len(set(paths)), "two games would share an archive path"
    return games


def write_archive(storm, path, files):
    """Streams files from disk into a new MPQ (no whole-archive copy in memory), then reads every one back."""
    import ctypes as c
    from cars_mount_pack import H, U
    dll = storm.dll
    dll.SFileAddFileEx.argtypes = [H, c.c_wchar_p, c.c_char_p, U, U, U]
    dll.SFileAddFileEx.restype = c.c_bool
    handle = H()
    # listfile + attributes, format V2 (no 4 GiB ceiling)
    if not dll.SFileCreateArchive(str(path), 0x00100000 | 0x00200000 | 0x01000000, len(files) + 16, c.byref(handle)):
        raise OSError(f"SFileCreateArchive {path}: {c.get_last_error()}")
    try:
        for name, source in files.items():
            if not dll.SFileAddFileEx(handle, str(source), name.encode("ascii"), 0x80000000, 0, 0xFFFFFFFF):
                raise OSError(f"SFileAddFileEx {name}: {c.get_last_error()}")
    finally:
        dll.SFileCloseArchive(handle)
    handle = storm.open_archive(path)
    try:
        for name, source in files.items():
            assert hashlib.sha256(storm.read(handle, name)).digest() == hashlib.sha256(source.read_bytes()).digest(), name
    finally:
        dll.SFileCloseArchive(handle)


def q(s):
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"


def mysql(sql, db):
    os.environ["MSYS_NO_PATHCONV"] = "1"
    r = subprocess.run(["docker", "exec", "-i", "ac-database", "sh", "-c", f'mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -N {db}'],
                       input=sql.encode(), capture_output=True)
    if r.returncode:
        raise SystemExit(r.stderr.decode())
    return r.stdout.decode()


def attract_blp(work):
    """The idle cabinet screen (Extensions/Arcade kAttract): low-res pixel art upscaled with hard edges."""
    from PIL import Image, ImageDraw, ImageFont
    sys.path.insert(0, str(REPO / "Tools/FortniteCosmetics"))
    from convert_backpacks import texture_bytes
    small = Image.new("RGB", (128, 128), (6, 8, 22))
    draw = ImageDraw.Draw(small)
    font = ImageFont.load_default(size=11)
    for y in range(0, 128, 8):  # faint grid, like a title screen backdrop
        draw.line([(0, y), (127, y)], fill=(12, 16, 40))
    def centered(text, y, fill):
        w = draw.textlength(text, font=font)
        draw.text(((128 - w) / 2, y), text, font=font, fill=fill)
    centered("ESTERIA", 26, (232, 196, 106))
    centered("ARCADE", 42, (232, 196, 106))
    draw.rectangle([22, 62, 105, 64], fill=(40, 90, 200))
    centered("INSERT COIN", 82, (240, 240, 240))
    big = small.resize((256, 256), Image.Resampling.NEAREST)
    lines = ImageDraw.Draw(big)
    for y in range(1, 256, 2):  # CRT lines
        lines.line([(0, y), (255, y)], fill=(0, 0, 0))
    png = work / "attract.png"
    Image.blend(big, small.resize((256, 256), Image.Resampling.NEAREST), 0.65).save(png)
    return texture_bytes(png)


def content_sql(games, spawn):
    top = max(g["id"] for g in games)
    lines = ["-- Generated by wowdecomp Tools/Arcade/pack.py",
             "ALTER TABLE `esteria_arcade_game` MODIFY `rom` TEXT NOT NULL;",  # tables made before multi-file games
             f"DELETE FROM `esteria_arcade_game` WHERE `id` BETWEEN 1 AND {top};"]
    for g in games:
        rom = "|".join(name for name, _ in g["files"])
        lines.append(f"INSERT INTO `esteria_arcade_game` (`id`, `title`, `system`, `core`, `rom`) VALUES "
                     f"({g['id']}, {q(g['title'])}, {q(g['system'])}, {q(CORES[g['system']])}, {q(rom)});")
    first, last = BASE_ENTRY + 1, BASE_ENTRY + top
    lines.append(f"DELETE FROM `gameobject_template` WHERE `entry` BETWEEN {first} AND {last};")
    lines.append(f"DELETE FROM `esteria_arcade_cabinet` WHERE `entry` = {FIRST_CABINET} OR `entry` BETWEEN {first} AND {last};")
    for g in games:
        name = (g["title"] + " Cabinet")[:100]
        display = GAME_DISPLAYS.get(g["title"], (DISPLAY,))[0]
        lines.append(f"INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `size`, `Data3`, `ScriptName`) "
                     f"VALUES ({BASE_ENTRY + g['id']}, 2, {display}, {q(name)}, 'Speak', 1, 3610001, 'go_esteria_arcade');")
        lines.append(f"INSERT INTO `esteria_arcade_cabinet` (`entry`, `game`, `price`) VALUES ({BASE_ENTRY + g['id']}, {g['id']}, {PRICE});")
    # Every cabinet is also housing decor (its own catalog piece, same gameobject): mod-esteria-housing places it,
    # mod-esteria-arcade makes it free for the house's owner. model_fdid 0x40000001 = Extensions/Housing's custom
    # model 1 (this cabinet); category 10 = the editor's Arcade tab.
    lines.append(f"DELETE FROM `housing_decor_catalog` WHERE `decor_id` BETWEEN {DECOR_BASE} AND {DECOR_BASE + 9999};")
    for g in games:
        lines.append(f"INSERT INTO `housing_decor_catalog` (`decor_id`, `model_fdid`, `display_id`, `gameobject_entry`, `name`, "
                     f"`category`, `surface`, `budget_cost`, `min_scale`, `max_scale`, `origin`, `enabled`) VALUES "
                     f"({DECOR_BASE + g['id']}, {GAME_DISPLAYS.get(g['title'], (0, 0x40000001))[1]}, "
                     f"{GAME_DISPLAYS.get(g['title'], (DISPLAY,))[0]}, {BASE_ENTRY + g['id']}, {q((g['title'] + ' Cabinet')[:100])}, "
                     f"10, 0, 1, 0.5, 1.5, 'custom', 1);")
    # The original Lionheart cabinet becomes a playable one too.
    lines.append(f"UPDATE `gameobject_template` SET `ScriptName` = 'go_esteria_arcade' WHERE `entry` = {FIRST_CABINET};")
    lines.append(f"INSERT INTO `esteria_arcade_cabinet` (`entry`, `game`, `price`) VALUES ({FIRST_CABINET}, 2, {PRICE});")
    if spawn:
        m, x, y, z, o = spawn
        side = o + math.pi / 2  # a row beside the original, all facing the same way
        lines.append(f"DELETE FROM `gameobject` WHERE `guid` BETWEEN {BASE_ENTRY} AND {BASE_ENTRY + 99};")
        for i, gid in enumerate(SHOWCASE, 1):
            gx, gy = x + 1.7 * i * math.cos(side), y + 1.7 * i * math.sin(side)
            lines.append(f"INSERT INTO `gameobject` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, "
                         f"`position_z`, `orientation`, `rotation2`, `rotation3`, `spawntimesecs`, `animprogress`, `state`, `Comment`) "
                         f"VALUES ({BASE_ENTRY + i}, {BASE_ENTRY + gid}, {m}, 1, 1, {gx:.3f}, {gy:.3f}, {z:.3f}, {o:.4f}, "
                         f"{math.sin(o / 2):.6f}, {math.cos(o / 2):.6f}, 0, 100, 1, 'Arcade cabinet');")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cores", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--install", action="store_true")
    ap.add_argument("--skip-system-archives", action="store_true", help="reuse the per-system archives already installed (new systems are still packed)")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)

    games = all_games()
    entries, receipt = {}, {"games": []}
    for game in GAMES:  # the base archive keeps exactly what it had: the first 15 games, the window, the attract screen
        data = game[3].read_bytes()
        entries[rom_path(game)] = data
    entries["Arcade\\Screens\\Attract.blp"] = attract_blp(args.out)
    for name in ("EsteriaArcade.toc", "esteria_style.lua", "EsteriaArcade.lua"):
        entries["Interface\\AddOns\\EsteriaArcade\\" + name] = (REPO / "Addons/EsteriaArcade" / name).read_bytes()
    needed = sorted(set(CORES[g["system"]] for g in games))
    cores = {name: (args.cores / f"{name}_libretro.dll") for name in needed}
    for name, path in cores.items():
        assert path.exists(), path
        receipt.setdefault("cores", {})[name] = hashlib.sha256(path.read_bytes()).hexdigest()

    sys.path.insert(0, str(REPO.parent / "EsteriaWoW/tools"))
    from cars_mount_pack import Storm
    storm = Storm(REPO / "Tools/MountStudio/runtime/backend/StormLib.dll")
    archives = [args.out / ARCHIVE]
    storm.create_archive(archives[0], entries)
    handle = storm.open_archive(archives[0])
    try:
        for name, data in entries.items():
            assert storm.read(handle, name) == data, name
    finally:
        storm.dll.SFileCloseArchive(handle)

    by_system = {}
    for g in games:
        if not g["base"]:
            by_system.setdefault(g["system"], {}).update({name: source for name, source in g["files"]})
    for system, files in sorted(by_system.items()):
        archive = args.out / SYSTEM_ARCHIVE.format(system.upper())
        if not args.skip_system_archives or not (CLIENT / "Data" / archive.name).exists():  # a new system still packs
            write_archive(storm, archive, files)
            archives.append(archive)
        print(f"{system}: {len(files)} files, {sum(f.stat().st_size for f in files.values()) >> 20} MiB", flush=True)
    for g in games:
        receipt["games"].append({"id": g["id"], "title": g["title"], "system": g["system"], "entry": BASE_ENTRY + g["id"],
                                 "rom": "|".join(name for name, _ in g["files"])})

    spawn = None
    row = mysql("SELECT map, position_x, position_y, position_z, orientation FROM gameobject WHERE guid = 9610001;",
                "housing_dev_world").split()
    if row:
        spawn = (int(row[0]), *map(float, row[1:]))
    (args.out / "arcade_content.sql").write_text(content_sql(games, spawn))
    (args.out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    (args.out / "cabinets.txt").write_text("".join(f".gob add {BASE_ENTRY + g['id']}    {g['system']:8} {g['title']}\n" for g in games))
    print(f"{len(games)} games in {len(archives)} new archive(s), cores {needed}")

    if args.install:
        backup = CLIENT / "_backups" / ("arcade-roms-" + time.strftime("%Y%m%d-%H%M%S"))
        backup.mkdir(parents=True)
        for archive in archives:
            target = CLIENT / "Data" / archive.name
            if target.exists():
                shutil.move(target, backup / archive.name)
            shutil.copy2(archive, target)
        core_dir = CLIENT / "Arcade" / "Cores"
        core_dir.mkdir(parents=True, exist_ok=True)
        for name, path in cores.items():
            dest = core_dir / path.name
            if dest.exists():
                shutil.copy2(dest, backup / path.name)
            shutil.copy2(path, dest)
        mysql((MODULE / "data/sql/db-world/esteria_arcade.sql").read_text() + (args.out / "arcade_content.sql").read_text(),
              "housing_dev_world")
        mysql((MODULE / "data/sql/db-characters/esteria_arcade.sql").read_text(), "housing_dev_characters")
        print("installed; backups in", backup)


if __name__ == "__main__":
    main()
