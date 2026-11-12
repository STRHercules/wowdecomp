"""Packs the Upright Undead models as a second undead body (WoW/Common/ComponentCore/UprightUndead.h).

The package (UprightUndead-3.3.5a: UprightUndead.MPQ + rows\\*.csv) puts its models at the stock undead paths. This
moves every file to Character\\Scourge\\Upright\\..., points the models' own texture names there, and adds the
package's customization rows to race 5 at index BASE and up (skin colour, face, hair style, hair colour and feature
indices all start at BASE), so the hunched body keeps its rows and the five appearance bytes say which body a
character has. The server needs nothing: it stores and sends the bytes unvalidated.

The three tables are taken from whichever client archive currently wins for them (today Patch-ZzzzzzzzEthereal.MPQ)
and the output archive must sort after it. Re-run this whenever an archive that ships those tables is rebuilt, or
the upright rows will be shadowed (the creator then hides the toggle's upright choices).

  python build.py --mod "G:\\Downloads\\HDWoWModels\\UprightUndead-3.3.5a" --client "G:\\3.3.5a - Dev"            # build
  python build.py --mod ... --client "G:\\3.3.5a - Dev" --install    # build, back up any old copy, copy into Data
"""

import argparse
import csv
import ctypes
import shutil
import struct
import subprocess
import sys
from datetime import datetime
from pathlib import Path

BASE = 64  # must match Upright::Base
RACE = 5
ARCHIVE = "Patch-ZzzzzzzzzUprightUndead.MPQ"  # nine z's: sorts after Patch-ZzzzzzzzEthereal.MPQ
OLD_PREFIX = "character\\scourge\\"
NEW_PREFIX = "Character\\Scourge\\Upright\\"
TABLES = ("CharSections", "CharHairGeosets", "CharacterFacialHairStyles")
SECTION_HAS_VARIATION = {1, 3}  # face and hair rows have their own variation; skin (0) and underwear (4) use 0


def stock_path(name):
    return name.lower().startswith(OLD_PREFIX) and not name.lower().startswith(NEW_PREFIX.lower())


def repath(name):
    """Character\\Scourge\\Male\\X -> Character\\Scourge\\Upright\\Male\\X (other paths unchanged)."""
    return NEW_PREFIX + name[len(OLD_PREFIX):] if stock_path(name) else name


def patch_archives(client):
    """The client's patch archives, highest priority first (PatchFiles: full paths sorted descending, case-blind)."""
    data = client / "Data"
    found = [p for p in data.glob("*") if p.is_file() and p.name.lower().startswith("patch-") and p.suffix.lower() == ".mpq"]
    for locale in data.iterdir():
        if locale.is_dir():
            found += [p for p in locale.glob("*") if p.is_file() and p.name.lower().startswith("patch-" + locale.name.lower() + "-")
                      and p.suffix.lower() == ".mpq"]
    return sorted(found, key=lambda p: str(p.relative_to(client)).upper(), reverse=True)


def winning_tables(storm, H, client):
    """Each table from the highest-priority archive that has it, skipping this tool's own archive."""
    tables, sources = {}, {}
    for path in patch_archives(client):
        if path.name.lower() == ARCHIVE.lower():
            continue
        handle = H()
        if not storm.dll.SFileOpenArchive(str(path), 0, 0x100, ctypes.byref(handle)):
            continue
        try:
            for table in TABLES:
                if table in tables:
                    continue
                try:
                    tables[table] = storm.read(handle, "DBFilesClient\\" + table + ".dbc")
                    sources[table] = path.name
                except OSError:
                    pass
        finally:
            storm.dll.SFileCloseArchive(handle)
        if len(tables) == len(TABLES):
            return tables, sources
    raise SystemExit("Not found in any patch archive: " + ", ".join(t for t in TABLES if t not in tables))


def read_wdbc(data):
    magic, count, fields, size, string_size = struct.unpack_from("<4s4I", data)
    if magic != b"WDBC" or size != fields * 4 or len(data) != 20 + count * size + string_size:
        raise SystemExit("Unexpected WDBC layout")
    rows = [list(struct.unpack_from("<%di" % fields, data, 20 + i * size)) for i in range(count)]
    return fields, rows, bytearray(data[20 + count * size:])


def write_wdbc(fields, rows, strings):
    body = b"".join(struct.pack("<%di" % fields, *row) for row in rows)
    return struct.pack("<4s4I", b"WDBC", len(rows), fields, fields * 4, len(strings)) + body + bytes(strings)


def add_string(strings, cache, value):
    if not value:
        return 0
    if value not in cache:
        cache[value] = len(strings)
        strings.extend(value.encode("ascii") + b"\0")
    return cache[value]


def load_rows(mod, name):
    with open(mod / "rows" / (name + ".csv"), newline="", encoding="utf-8") as handle:
        return list(csv.reader(handle))[1:]


def build_tables(tables, mod, packed):
    """Returns the three tables with the upright rows (race 5, index BASE and up) in place of any older copy."""
    out = {}
    missing = set()

    # CharSections: ID, Race, Sex, BaseSection, Texture[3], Flags, VariationIndex, ColorIndex.
    fields, rows, strings = read_wdbc(tables["CharSections"])
    rows = [r for r in rows if not (r[1] == RACE and (r[8] >= BASE or r[9] >= BASE))]
    next_id, cache = max(r[0] for r in rows) + 1, {}
    for sex, section, t1, t2, t3, flags, variation, color in (row[1:9] for row in load_rows(mod, "CharSections")):
        textures = []
        for texture in (t1, t2, t3):
            moved = repath(texture)
            if texture and moved.lower() not in packed:
                missing.add(texture)
            textures.append(add_string(strings, cache, moved))
        section = int(section)
        rows.append([next_id, RACE, int(sex), section, *textures, int(flags),
                     int(variation) + (BASE if section in SECTION_HAS_VARIATION else 0), int(color) + BASE])
        next_id += 1
    out["CharSections"] = write_wdbc(fields, rows, strings)

    # CharHairGeosets: ID, Race, Sex, VariationID, GeosetID, Showscalp.
    fields, rows, strings = read_wdbc(tables["CharHairGeosets"])
    rows = [r for r in rows if not (r[1] == RACE and r[3] >= BASE)]
    next_id = max(r[0] for r in rows) + 1
    for sex, variation, geoset, scalp in (row[1:5] for row in load_rows(mod, "CharHairGeosets")):
        rows.append([next_id, RACE, int(sex), int(variation) + BASE, int(geoset), int(scalp)])
        next_id += 1
    out["CharHairGeosets"] = write_wdbc(fields, rows, strings)

    # CharacterFacialHairStyles: Race, Sex, VariationID, Geoset[5] (groups 1, 3, 2, 16, 17).
    fields, rows, strings = read_wdbc(tables["CharacterFacialHairStyles"])
    rows = [r for r in rows if not (r[0] == RACE and r[2] >= BASE)]
    for sex, variation, *geosets in (row[1:8] for row in load_rows(mod, "CharacterFacialHairStyles")):
        rows.append([RACE, int(sex), int(variation) + BASE, *map(int, geosets)])
    out["CharacterFacialHairStyles"] = write_wdbc(fields, rows, strings)
    return out, missing


def check(entries):
    """Self-check of what the client will read: the tables' upright rows and every file they and the models name."""
    names = {name.lower() for name in entries}
    _, sections, strings = read_wdbc(entries["DBFilesClient\\CharSections.dbc"])
    text = lambda offset: strings[offset:strings.index(b"\0", offset)].decode("ascii")
    for sex, skins in ((0, 16), (1, 16)):
        got = sorted(r[9] for r in sections if r[1:4] == [RACE, sex, 0] and r[9] >= BASE)
        assert got == list(range(BASE, BASE + skins)), ("skin rows", sex, got)
        for r in sections:
            if r[1] == RACE and r[2] == sex and r[9] >= BASE:
                for offset in r[4:7]:
                    assert not offset or text(offset).lower() in names, ("texture not packed", text(offset))
    keys = [(r[1], r[2], r[3], r[8], r[9]) for r in sections]
    assert keys == sorted(keys), "CharSections must stay sorted (BuildComponentArray groups contiguous rows)"
    _, hair, _ = read_wdbc(entries["DBFilesClient\\CharHairGeosets.dbc"])
    assert sorted(r[3] for r in hair if r[1:3] == [RACE, 0] and r[3] >= BASE) == list(range(BASE, BASE + 21))
    _, facial, _ = read_wdbc(entries["DBFilesClient\\CharacterFacialHairStyles.dbc"])
    assert sorted(r[2] for r in facial if r[:2] == [RACE, 0] and r[2] >= BASE) == list(range(BASE, BASE + 55))
    assert max(r[2] for r in facial if r[0] == RACE) < 256 and max(r[9] for r in sections if r[1] == RACE) < 256, \
        "appearance bytes must fit in a byte"
    for model in ("Character\\Scourge\\Upright\\Male\\ScourgeMale.m2", "Character\\Scourge\\Upright\\Female\\ScourgeFemale.m2"):
        assert model.lower() in names, ("model missing", model)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mod", type=Path, required=True, help="UprightUndead-3.3.5a folder (MPQ + rows)")
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=Path(__file__).with_name(ARCHIVE))
    parser.add_argument("--tools", type=Path, default=Path(r"R:\Users\Zach\Documents\GitHub\EsteriaWoW\tools"),
                        help="EsteriaWoW tools folder (provides the StormLib wrapper)")
    parser.add_argument("--install", action="store_true", help="copy the archive into <client>\\Data (backs up an old copy)")
    args = parser.parse_args()

    sys.path.insert(0, str(args.tools))
    from cars_mount_pack import DLL_DEFAULT, H, Storm, order_charsections_entry, rewrite_m2_texture_paths, _m2_texture_records  # noqa: E402

    repo_storm = Path(__file__).resolve().parents[1] / "MountStudio" / "runtime" / "backend" / "StormLib.dll"
    storm = Storm(DLL_DEFAULT if DLL_DEFAULT.exists() else repo_storm)

    source = storm.open_archive(args.mod / "UprightUndead.MPQ")
    try:
        files = {name: storm.read(source, name) for name, *_ in storm.list_files(source)
                 if name.lower().startswith(OLD_PREFIX)}
    finally:
        storm.dll.SFileCloseArchive(source)
    entries = {repath(name): data for name, data in files.items()}
    packed = {name.lower() for name in entries}

    # The models name some textures themselves; point those at the moved copies (leave any the package lacks).
    for name in [n for n in entries if n.lower().endswith(".m2")]:
        own = {old: repath(old.decode("ascii")) for _, _, old in _m2_texture_records(entries[name])
               if old and repath(old.decode("ascii")).lower() in packed}
        entries[name] = rewrite_m2_texture_paths(entries[name], own)
        left = [old.decode("ascii") for _, _, old in _m2_texture_records(entries[name]) if old and stock_path(old.decode("ascii"))]
        if left:
            print("note:", name, "still names stock-path textures:", ", ".join(left))

    tables, sources = winning_tables(storm, H, args.client)
    built, missing = build_tables(tables, args.mod, packed)
    for table, data in built.items():
        entries["DBFilesClient\\" + table + ".dbc"] = order_charsections_entry("DBFilesClient\\" + table + ".dbc", data)
    if missing:
        print("note: rows name textures the package lacks (left at their stock path):", ", ".join(sorted(missing)))
    check(entries)
    print("tables from:", ", ".join(table + " <- " + src for table, src in sources.items()))

    # Compressed (MPQ_FILE_COMPRESS, zlib), unlike the wrapper's create_archive.
    if args.out.exists():
        args.out.unlink()
    handle = H()
    if not storm.dll.SFileCreateArchive(str(args.out), 0x1 | 0x2, len(entries) + 16, ctypes.byref(handle)):
        raise SystemExit("SFileCreateArchive failed: " + str(args.out))
    try:
        for name, payload in entries.items():
            file_handle = H()
            if not storm.dll.SFileCreateFile(handle, name.encode("ascii"), 0, len(payload), 0, 0x200, ctypes.byref(file_handle)):
                raise SystemExit("SFileCreateFile failed: " + name)
            try:
                if not storm.dll.SFileWriteFile(file_handle, ctypes.create_string_buffer(payload or b"\0"), len(payload), 0x2):
                    raise SystemExit("SFileWriteFile failed: " + name)
            finally:
                storm.dll.SFileFinishFile(file_handle)
    finally:
        storm.dll.SFileCloseArchive(handle)

    readback = storm.open_archive(args.out)
    try:
        assert all(storm.read(readback, name) == data for name, data in entries.items()), "readback mismatch"
    finally:
        storm.dll.SFileCloseArchive(readback)
    print("wrote", args.out, "(%d files, %.1f MiB)" % (len(entries), args.out.stat().st_size / 1048576))

    if args.install:
        running = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
        if "wow.exe" in running or "wowclient" in running:
            raise SystemExit("Close the game first; the archives are in use.")
        target = args.client / "Data" / ARCHIVE
        if target.exists():
            backup = args.client / "_backups" / ("upright-undead-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
            backup.mkdir(parents=True)
            shutil.copy2(target, backup / ARCHIVE)
            print("backup", backup / ARCHIVE)
        shutil.copy2(args.out, target)
        print("installed", target)


if __name__ == "__main__":
    main()
