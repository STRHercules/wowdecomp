"""Resolve the owner's ROM master list (2026-11-22) to files: one regex per title, matched against file names in the
system folder. Prints every match so ambiguous or missing titles can be fixed before catalog.json is written.

    python resolve_catalog.py [--write catalog.json]
"""
import hashlib
import json
import re
import sys
from pathlib import Path

V = Path("V:/ROMs")
E = Path("E:/ROMs")

# system, folder(s), [(title, regex on file name)]
WANTED = [
    ("nes", [V / "NES"], [
        ("Super Mario Bros. / Duck Hunt / Track Meet", r"^3-in-1 Super Mario Bros"),
        ("Tetris", r"^Tetris \(Nintendo\)"), ("Dr. Mario", r"^Dr\. Mario"), ("Excitebike", r"^Excitebike"),
        ("Golf", r"^Golf\.nes$|^Golf \(U"), ("Dragon Warrior III", r"^Dragon Warrior III"), ("Kung Fu", r"^Kung Fu\."),
        ("Baseball", r"^Baseball\.nes$|^Baseball \(U"), ("Dragon Warrior IV", r"^Dragon Warrior IV"),
        ("Punch-Out!!", r"^Punch-Out|^Mike Tyson"), ("Dragon Warrior II", r"^Dragon Warrior II\b(?!I)"),
        ("Ice Hockey", r"^Ice Hockey"), ("Pro Wrestling", r"^Pro Wrestling"), ("Tennis", r"^Tennis\.nes$|^Tennis \(U"),
        ("Volleyball", r"^Volleyball"), ("R.C. Pro-Am", r"^R\.?C\.? Pro[- ]Am(?! II)"),
        ("Dragon Warrior", r"^Dragon Warrior\.nes$|^Dragon Warrior \("), ("Top Gun", r"^Top Gun\.nes$|^Top Gun \("),
    ]),
    ("n64", [V / "Nintendo 64"], [
        ("Super Mario 64", r"^Super Mario 64"), ("GoldenEye 007", r"^GoldenEye"), ("Mario Kart 64", r"^Mario Kart 64"),
        ("The Legend of Zelda: Ocarina of Time", r"^Legend of Zelda - Ocarina of Time\.z64"),
        ("Ocarina of Time - Master Quest", r"Master Quest"), ("Super Smash Bros.", r"^Smash Brothers"),
        ("Star Fox 64", r"^Star Fox 64"), ("Donkey Kong 64", r"^Donkey Kong 64"),
        ("The Legend of Zelda: Majora's Mask", r"Majora"), ("Banjo-Kazooie", r"^Banjo-Kazooie"),
        ("Star Wars: Rogue Squadron", r"Rogue Squadron"), ("Perfect Dark", r"^Perfect Dark"),
        ("Turok 2: Seeds of Evil", r"^Turok 2"), ("Turok: Dinosaur Hunter", r"^Turok - Dinosaur"),
        ("Mario Party 3", r"^Mario Party 3"), ("Mortal Kombat Trilogy", r"^Mortal Kombat Trilogy"),
        ("Mortal Kombat 4", r"^Mortal Kombat 4"), ("Toy Story 2", r"^Toy Story 2"), ("Excitebike 64", r"^Excitebike 64"),
        ("Kirby 64: The Crystal Shards", r"^Kirby 64"), ("Paper Mario", r"^Paper Mario"),
        ("Tony Hawk's Pro Skater 2", r"^Tony Hawk"), ("Duke Nukem 64", r"^Duke Nukem 64"),
    ]),
    ("gb", [V / "Nintendo GameBoy"], [
        ("Pokemon Red", r"^Pokemon Red\.gb$"), ("Pokemon Blue", r"^Pokemon Blue\.gb$"),
        ("Pokemon Yellow", r"^Pokemon Yellow\.gb$"), ("Pokemon Trading Card Game", r"^Pokemon Trading Card Game"),
        ("Pokemon Brown", r"^Pokemon Brown\.gb$"), ("Pokemon Grape", r"^Pokemon Grape\.gb$"),
    ]),
    ("gbc", [V / "Nintendo GameBoy Color"], [
        ("Pokemon Gold", r"^Pokemon - Gold Version"), ("Pokemon Silver", r"Silver"),
        ("Pokemon Crystal", r"^Pokemon - Crystal Version"), ("Link's Awakening DX", r"Link's Awakening DX"),
        ("Oracle of Ages", r"Oracle of Ages"), ("Oracle of Seasons", r"Oracle of Seasons"),
        ("Pokemon Puzzle Challenge", r"Puzzle Challenge"), ("Pokemon Crystal Clear", r"Crystal[ _]Clear"),
        ("Pokemon Prism", r"Prism"), ("Pokemon Pink", r"Pink"), ("Pokemon Red (GS Prototype Sprites)", r"GS Proto|Prototype"),
        ("Pokemon Yellow (GBC)", r"^Pokemon - Yellow Version"),
    ]),
    ("gba", [V / "Nintendo GameBoy Advance", E / "Nintendo Gameboy Advance"], [
        ("Pokemon Ruby", r"^Pokemon Ruby\.|^Pokemon - Ruby"), ("Pokemon Sapphire", r"^Pokemon Sapphire"),
        ("Pokemon FireRed", r"^Pokemon - Fire Red\.|^Pokemon Fire Red\.gba$|^Pokemon - FireRed"),
        ("Pokemon LeafGreen", r"^Pokemon - Leaf Green|^Pokemon Leaf Green\."),
        ("Pokemon Emerald", r"^Pokemon - Emerald Version"),
        ("Pokemon Mystery Dungeon: Red Rescue Team", r"Mystery Dungeon"), ("Yu-Gi-Oh! The Eternal Duelist Soul", r"Eternal Duelist"),
        ("Dragon Ball Z: The Legacy of Goku", r"Legacy [Oo]f Goku \(U\)\(Mode7\)|Legacy Of Goku \("),
        ("Pokemon Pinball: Ruby & Sapphire", r"Pinball"), ("Dragon Ball Z: The Legacy of Goku II", r"Legacy of Goku II"),
        ("Yu-Gi-Oh! The Sacred Cards", r"Sacred Cards"), ("Dragon Ball Z: Buu's Fury", r"Buu's Fury"),
        ("Dragon Ball Z: Supersonic Warriors", r"Supersonic Warriors"), ("Yu-Gi-Oh! GX: Duel Academy", r"Duel Academy"),
        ("Yu-Gi-Oh! Ultimate Masters 2006", r"Ultimate Masters"), ("Dragon Ball GT: Transformation", r"GT - Transformation"),
        ("Baldur's Gate: Dark Alliance", r"Baldur"),
    ]),
    ("genesis", [V / "SEGA"], [
        ("Sonic the Hedgehog", r"^Sonic The Hedgehog \(W\)"), ("Sonic the Hedgehog 2", r"^Sonic The Hedgehog 2 \("),
        ("Sonic the Hedgehog 3", r"^Sonic The Hedgehog 3 \("), ("Sonic & Knuckles", r"^Sonic and Knuckles \(W\)"),
        ("Mortal Kombat", r"^Mortal Kombat \("), ("Disney's Aladdin", r"^Aladdin|Disney's Aladdin"),
        ("John Madden Football '93", r"^John Madden Football '93 \(U\)"), ("NHL '94", r"^NHL Hockey 94"),
        ("Joe Montana Football", r"^Joe Montana Football"), ("Jurassic Park", r"^Jurassic Park \("),
        ("NBA Jam", r"^NBA Jam \("), ("Mortal Kombat II", r"^Mortal Kombat II"),
        ("Street Fighter II': Special Champion Edition", r"Special Champion"), ("Eternal Champions", r"^Eternal Champions"),
        ("FIFA Soccer 95", r"^FIFA Soccer 95"), ("Altered Beast", r"^Altered Beast"), ("Mortal Kombat 3", r"^Mortal Kombat 3"),
        ("X-Men", r"^X-Men \("), ("Ms. Pac-Man", r"^Ms\.? Pac-Man"), ("Streets of Rage", r"^Streets of Rage \("),
        ("Streets of Rage 2", r"^Streets of Rage 2"), ("Phantasy Star IV", r"^Phantasy Star - The End"),
    ]),
    ("snes", [V / "SNES"], [
        ("Street Fighter II: The World Warrior", r"^Street Fighter II(?! Turbo)(?!.*Super)"),
        ("Street Fighter II Turbo", r"^Street Fighter II Turbo"), ("Killer Instinct", r"^Killer Instinct"),
        ("F-Zero", r"^F-Zero"), ("Mario Paint", r"^Mario Paint"), ("Pilotwings", r"^Pilotwings"),
        ("Super Street Fighter II", r"^Super Street Fighter II"), ("SimCity", r"^SimCity\.|^Sim City\.|^SimCity \("),
        ("Secret of Mana", r"^Secret of Mana"), ("Final Fight", r"^Final Fight\.|^Final Fight \("),
        ("The Magical Quest Starring Mickey Mouse", r"Magical Quest"), ("Ken Griffey Jr. Presents Major League Baseball", r"Ken Griffey Jr\. Presents"),
        ("Super Ghouls 'n Ghosts", r"Super Ghouls"), ("Final Fight 2", r"^Final Fight 2"), ("Tetris 2", r"^Tetris 2"),
        ("Space Invaders", r"^Space Invaders"), ("Super Bomberman 2", r"^Super Bomberman 2"),
        ("Contra III: The Alien Wars", r"^Contra III"), ("ActRaiser", r"^ActRaiser\.smc$"), ("Tetris Attack", r"^Tetris Attack"),
        ("Tetris & Dr. Mario", r"^Tetris & Dr"),
    ]),
    ("ps1", [V / "Sony Playstation/Spyro - Year of the Dragon"], [("Spyro: Year of the Dragon", r"\.cue$")]),
]
EXT = {"nes": {".nes"}, "n64": {".n64", ".v64", ".z64"}, "gb": {".gb"}, "gbc": {".gbc", ".gb"}, "gba": {".gba"},
       "genesis": {".bin", ".md", ".gen", ".smd"}, "snes": {".smc", ".sfc"}, "ps1": {".cue"}}
MAME_FAILED = {"ddragon", "mario", "mk"}  # FBNeo: wrong set version or black screen (2026-11-22 smoke test)


def files(folders, system):
    out = []
    for folder in folders:
        for p in folder.rglob("*"):
            if p.is_file() and p.suffix.lower() in EXT[system]:
                out.append(p)
    return out


def main():
    rows, seen, problems = [], {}, 0
    for system, folders, titles in WANTED:
        pool = files(folders, system)
        for title, pattern in titles:
            hits = [p for p in pool if re.search(pattern, p.name, re.I)]
            if system == "gbc" and title in ("Pokemon Crystal Clear", "Pokemon Prism"):
                matched = hits  # every build is its own cabinet
            else:
                matched = hits[:1]
                if len(hits) != 1:
                    print(f"  {'MISSING' if not hits else 'AMBIGUOUS'} {system} {title}: {[p.name for p in hits]}")
                    problems += not hits
            for p in matched:
                rows.append({"system": system, "title": title if len(matched) == 1 else f"{title} ({p.stem})", "file": str(p)})
        if system == "gba":  # every Pokemon hack on E:
            for p in sorted(files([E / "Nintendo Gameboy Advance"], "gba")):
                if "pokemon" in p.name.lower():
                    rows.append({"system": "gba", "title": p.stem.replace("Pokemon", "Pokemon").strip(), "file": str(p)})
    for z in sorted((V / "MAME").glob("*.zip")):
        if z.stem != "neogeo" and z.stem not in MAME_FAILED:
            rows.append({"system": "arcade", "title": z.stem, "file": str(z)})
    # One cabinet per distinct file content.
    unique = []
    for row in rows:
        digest = hashlib.sha256(Path(row["file"]).read_bytes()).hexdigest()
        if digest in seen:
            continue
        seen[digest] = row
        row["sha256"] = digest
        unique.append(row)
    for row in unique:
        print(f"{row['system']:8} {row['title'][:48]:48} {Path(row['file']).name}")
    print(f"{len(unique)} unique of {len(rows)}; {problems} missing")
    if "--write" in sys.argv:
        Path(sys.argv[sys.argv.index("--write") + 1]).write_text(json.dumps(unique, indent=1) + "\n")


if __name__ == "__main__":
    main()
