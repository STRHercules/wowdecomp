"""Write EsteriaMounts/custom.csv: which installed mounts the Customize window offers, and how.

Usage: mount_custom.py <reader> <definitions> <tables-dir> <output.csv> <Data/Retail/Packs>
  tables-dir: the pinned build's DB2s, as for mount_skins.py (the client's appearance resolver reads the same build).
Rows (WoW/Common/ComponentCore/MountCustomize.cpp):
  display;m;chrModel                       a ChrModel mount: options, choices and drawing come from the retail tables
  display;d;choice                         the display's own look (retail's chosen variant), one row per option
  display;o;option;name                    a client-defined option (no ChrModel), e.g. the D.R.I.V.E.'s paint
  display;c;option;choice;swatchARGB;name  one of its choices
  display;t;choice;textureType;fileDataId  what that choice puts on the model
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mount_skins import TABLES, customization  # noqa: E402

PAINTS = [("Factory", 0xFF8A8D92), ("Blue", 0xFF2B6CD6), ("Green", 0xFF3FA34D), ("Red", 0xFFC0392B), ("Yellow", 0xFFE5B92E)]
# Client-defined option sets by retail display: the G-99 Breakneck (D.R.I.V.E.) has no ChrModel; its garage paints
# are its displays' texture variations: slot 11 the body (*_1.blp), slot 12 the trim (*_2.blp).
SYNTHETIC = {124514: [
    (1, "Body Paint", 11, [6236355, 6310327, 6310355, 6310357, 6310359]),
    (2, "Trim Paint", 12, [6236357, 6310326, 6310354, 6310356, 6310358]),
]}


def synthetic_rows(target, options):
    rows = []
    for option, name, texture_type, files in options:
        rows.append(f"{target};o;{option};{name}")
        for index, ((paint, swatch), fid) in enumerate(zip(PAINTS, files)):
            choice = option * 100 + index + 1
            rows.append(f"{target};c;{option};{choice};{swatch};{paint}")
            rows.append(f"{target};t;{choice};{texture_type};{fid}")
        rows.append(f"{target};d;{option * 100 + 1}")
    return rows


def main():
    reader, definitions, source, output, packs = sys.argv[1:6]
    sys.path.insert(0, reader)
    from wotlkconv.db.db2 import parse_db2
    from wotlkconv.db.dbd import DbdIndex
    index = DbdIndex(definitions)
    tables = {name: parse_db2((Path(source) / f"{name}.db2").read_bytes(), name + ".db2", index).rows for name in TABLES}
    lines = []
    for manifest_path in sorted(Path(packs).glob("*/manifest.json")):
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        source_info, target = manifest["source"], manifest["target"]
        if target.get("kind") != "mount":
            continue
        display, esteria = source_info["retail_display_id"], target["ids"]["CreatureDisplayInfo"]
        if display in SYNTHETIC:
            lines += synthetic_rows(esteria, SYNTHETIC[display])
            continue
        if "fourth_texture" in source_info or display not in tables["CreatureDisplayInfo"]:
            continue  # imported from another build: its display ID means something else in these tables
        custom = customization(tables, display)
        if not custom:
            continue
        _, chosen, model = custom
        lines.append(f"{esteria};m;{model}")
        lines += [f"{esteria};d;{choice}" for choice in sorted(chosen)]
    assert all(line.split(";")[1] in "mdoct" for line in lines)
    Path(output).write_text("# display;m;chrModel | display;d;choice | display;o;option;name | display;c;option;choice;swatch;name"
                            " | display;t;choice;textureType;fileDataId\n" + "\n".join(lines) + "\n", encoding="utf-8")
    print("customizable displays", len({line.split(";")[0] for line in lines}))


if __name__ == "__main__":
    main()
