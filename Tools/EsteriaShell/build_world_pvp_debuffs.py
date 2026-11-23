"""Add the World PvP debuffs to a copy of the active additive Spell.dbc."""

import argparse
import ast
from pathlib import Path
import re
import struct


HEADER = struct.Struct("<4s4I")
ROW = struct.Struct("<234I")
SPELL_IDS = {955080, 955081, 955082, 955083}


def build(base, migration):
    schema = migration.parents[2] / "base/db_world/spell_dbc.sql"
    columns = re.findall(r"^  `([^`]+)`", schema.read_text(encoding="utf-8"), re.MULTILINE)
    if len(columns) != 234:
        raise ValueError("Unexpected Spell.dbc schema")
    insert = re.search(r"INSERT INTO `spell_dbc`\s*\((.*?)\)\s*VALUES\s*(.*?);",
                       migration.read_text(encoding="utf-8"), re.DOTALL)
    if not insert:
        raise ValueError("Expected spell_dbc insert in the migration")
    names = re.findall(r"`([^`]+)`", insert[1])
    indexes = [columns.index(name) for name in names]
    spells = ast.literal_eval("[" + insert[2] + "]")
    ids = {spell[names.index("ID")] for spell in spells}
    if ids != SPELL_IDS or len(spells) != len(ids):
        raise ValueError("Unexpected World PvP spell IDs")

    tag, count, fields, stride, string_size = HEADER.unpack_from(base)
    start = HEADER.size + count * stride
    if (tag, fields, stride) != (b"WDBC", len(columns), ROW.size) or len(base) != start + string_size:
        raise ValueError("Unexpected Spell.dbc format")
    rows = base[HEADER.size:start]
    pool = bytearray(base[start:])
    if not pool or pool[0] != 0 or pool[-1] != 0:
        raise ValueError("Invalid Spell.dbc string pool")
    if any(row[0] in SPELL_IDS for row in ROW.iter_unpack(rows)):
        raise ValueError("World PvP spell IDs are already present in the registry")

    added = bytearray()
    for spell in spells:
        row = [0] * fields
        for index, value in zip(indexes, spell, strict=True):
            if isinstance(value, str):
                row[index] = len(pool)
                pool.extend(value.encode("utf-8") + b"\0")
            else:
                row[index] = value
        added.extend(ROW.pack(*row))
    return HEADER.pack(tag, count + len(spells), fields, stride, len(pool)) + rows + added + pool


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path, help="Active additive registry Spell.dbc")
    parser.add_argument("migration", type=Path, help="World PvP status debuff migration in EsteriaWoW")
    parser.add_argument("output", type=Path, help="Spell.dbc in the new registry")
    args = parser.parse_args()
    data = build(args.base.read_bytes(), args.migration.resolve())
    with args.output.open("xb") as output:
        output.write(data)


if __name__ == "__main__":
    main()
