"""Port CoA's Manastorm (mod-manastorm-endless) world/character data and server DBC rows to Esteria.

    python -I manastorm_port.py rebuild   # CoA world package + Manastorm SQL -> scratch schema ms_coa_world
    python -I manastorm_port.py stage     # ID closure, clash remaps -> G:/ManastormPort/stage (SQL + DBC continuations)
    python -I manastorm_port.py test      # apply the stage to copies of Esteria's DBs (ms_test_world/characters)

Policy (user, 2026-11-12): keep CoA IDs where Esteria lacks them; an ID Esteria already uses for something else is
remapped (items 3300001+, spells 3800001+), as is a CoA ID above the
client's 5,000,000 table limit; a row both sides have that is the same thing keeps Esteria's copy.
Only scratch schemas are written here; installing on a live realm is a separate, explicit step.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import struct
import subprocess
import sys
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
COA = pathlib.Path("R:/Users/Zach/Documents/GitHub/azerothcore-wotlk-coa")
ESTERIA = pathlib.Path("R:/Users/Zach/Documents/GitHub/EsteriaWoW")
MODULE_SQL = COA / "modules/mod-manastorm-endless/data/sql"
PACKAGE = COA / "data/coa-world/coa-world-20260912.zip"
WORK = pathlib.Path("G:/ManastormPort")
COA_DBC, EST_DBC, EST_CONT, STAGE = WORK / "coa-dbc", WORK / "est-dbc", WORK / "est-cont", WORK / "stage"
EST_SPELL = ESTERIA / "modules/mod-custom-server/data/dbc/retroported-races/Spell.dbc"
COA_SCHEMA, STAGE_SCHEMA = "ms_coa_world", "ms_stage_world"
ITEM_REMAP_START, SPELL_REMAP_START, LIMIT_REMAP_START = 3300001, 3800001, 30001
# The client's supplemental tables refuse IDs above this (Engine/Source/Retail/ClientTables.cpp): remap past it too.
CLIENT_ID_LIMIT = 5000000
CONTINUATION = "manastorm"

# CoA migrations outside the world package that belong to Manastorm (all others are unrelated CoA content).
PENDING_WORLD = ["rev_20260920_00_manastorm_shared_tools.sql", "rev_20261023_90_manastorm_endgame_scaling.sql",
                 "rev_20261024_90_manastorm_endgame_group_damage.sql", "rev_20261028_10_manastorm_orgrimmar_hub_pad.sql",
                 "zz_manastorm_cogsworth_ranks.sql", "zz_manastorm_treasure_keeper_attackable.sql",
                 "zz_manastorm_tuning_curve.sql", "zzzz_rev_20261025_100_manastorm_s9_classic_cache_progression.sql",
                 "rev_1793141026264189233.sql", "rev_1793141678923622429.sql"]
ENDGAME_RESEED = ["rev_20261023_90_manastorm_endgame_scaling.sql", "rev_20261024_90_manastorm_endgame_group_damage.sql"]
# rev_20260908_01 creates ascension_manastorm_loadout, which the module's loadout statements still use.
PENDING_CHARACTERS = ["rev_1792493075163811100.sql", "rev_1792515600000000000.sql", "rev_20260908_00_manastorm.sql",
                      "rev_20260908_01_manastorm_progression.sql"]

# WDBC layouts: field count and string fields (localized names: 16 locale strings; the 17th slot is a flag mask).
LOC = lambda first: tuple(range(first, first + 16))  # noqa: E731
DBC = {
    "Spell": (234, LOC(136) + LOC(153) + LOC(170) + LOC(187)),
    "SpellDuration": (4, ()), "SpellCastTimes": (4, ()), "SpellRadius": (4, ()),
    "SpellRange": (40, LOC(6) + LOC(23)),
    "SpellItemEnchantment": (38, LOC(14)),
    "ItemSet": (53, LOC(1)), "ItemLimitCategory": (20, LOC(1)), "ItemExtendedCost": (16, ()),
    "ItemRandomProperties": (24, (1,) + LOC(7)), "ItemRandomSuffix": (29, LOC(1) + (18,)),
    "ScalingStatDistribution": (22, ()),
}
SPELL_REFS = (24, 25, 26, 27, 116, 117, 118)  # aura-state spells and effect trigger spells
SPELL_ENCHANT_EFFECTS = {53, 54, 92, 156}     # enchant item / temporary / held / prismatic: MiscValue = enchant
ENCHANT_SPELL_TYPES = {1, 3, 7}               # combat / equip / use spell: EffectArg = spell


# ---------------------------------------------------------------------------------------------- database access
def mysql(sql: str | bytes, schema: str = "", timeout: int | None = None) -> str:
    command = ["docker", "exec", "-i", "ac-database", "sh", "-c",
               'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --skip-column-names '
               "--default-character-set=utf8mb4 --max-allowed-packet=1G " + schema]
    data = sql if isinstance(sql, bytes) else sql.encode("utf-8")
    result = subprocess.run(command, input=data, capture_output=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace")[:3000])
    return result.stdout.decode("utf-8", errors="replace").strip()


def column(sql: str, schema: str = COA_SCHEMA) -> list[int]:
    return [int(x) for x in mysql(sql, schema).split()]


def dump(schema: str, tables: list[str], *extra: str) -> bytes:
    command = ["docker", "exec", "ac-database", "sh", "-c",
               'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysqldump -uroot --default-character-set=utf8mb4 '
               "--single-transaction --skip-lock-tables --skip-add-locks --skip-comments --skip-tz-utc "
               "--extended-insert --complete-insert --hex-blob " + " ".join(extra) + f" {schema} " + " ".join(tables)]
    result = subprocess.run(command, capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace")[:3000])
    return result.stdout


# --------------------------------------------------------------------------------------------------- WDBC files
class Table:
    """Rows of one WDBC table keyed by ID, with strings decoded per the DBC layout."""

    def __init__(self, name: str):
        self.name, (self.fields, self.strings) = name, DBC[name]
        self.rows: dict[int, list[int]] = {}
        self.text: dict[tuple[int, int], str] = {}

    def add_file(self, path: pathlib.Path, replace: bool = True) -> "Table":
        data = path.read_bytes()
        magic, count, fields, size, length = struct.unpack_from("<4s4I", data)
        if magic != b"WDBC" or fields != self.fields or size != fields * 4:
            raise ValueError(f"{path}: unexpected layout {fields} fields")
        pool = data[20 + count * size:]
        for index in range(count):
            row = list(struct.unpack_from(f"<{fields}I", data, 20 + index * size))
            if row[0] in self.rows and not replace:
                continue
            self.rows[row[0]] = row
            for field in self.strings:
                # some rows (CoA and Esteria alike) hold junk past the string block in unused locale slots: read as ""
                if 0 < row[field] < len(pool):
                    self.text[row[0], field] = pool[row[field]:pool.index(b"\0", row[field])].decode("utf-8", "replace")
                else:
                    self.text.pop((row[0], field), None)
        return self

    def name_of(self, row_id: int, field: int) -> str:
        return self.text.get((row_id, field), "")

    def build(self, ids: list[int], remap_row) -> bytes:
        rows, pool, offsets = [], bytearray(b"\0"), {"": 0}
        for row_id in ids:
            row = remap_row(list(self.rows[row_id]))
            for field in self.strings:
                value = self.text.get((row_id, field), "")
                if value not in offsets:
                    offsets[value] = len(pool)
                    pool.extend(value.encode("utf-8") + b"\0")
                row[field] = offsets[value]
            rows.append(row)
        return (struct.pack("<4s4I", b"WDBC", len(rows), self.fields, self.fields * 4, len(pool)) +
                b"".join(struct.pack(f"<{self.fields}I", *(v & 0xFFFFFFFF for v in row)) for row in rows) + bytes(pool))


def coa_table(name: str) -> Table:
    return Table(name).add_file(COA_DBC / f"{name}.dbc")


def esteria_table(name: str) -> Table:
    """Esteria's runtime view: base DBC (Spell from its bind-mounted file) plus every server continuation."""
    table = Table(name).add_file(EST_SPELL if name == "Spell" else EST_DBC / f"{name}.dbc")
    for path in sorted(EST_CONT.glob(f"{name}.dbc1-*")):
        if not path.name.endswith("-" + CONTINUATION):
            table.add_file(path)
    return table


# ---------------------------------------------------------------------------------------------------- rebuild
def ordered_sql(kind: str) -> list[pathlib.Path]:
    if kind == "world":
        base = sorted((MODULE_SQL / "db-world/base").glob("*.sql"), key=lambda p: p.name)
        updates = list((MODULE_SQL / "db-world/updates").glob("*.sql"))
        updates += [COA / "data/sql/updates/pending_db_world" / name for name in PENDING_WORLD]
    else:
        base = sorted((MODULE_SQL / "db-characters/base").glob("*.sql"), key=lambda p: p.name)
        updates = [COA / "data/sql/updates/pending_db_characters" / name for name in PENDING_CHARACTERS]
    # CoA's updater applies module and core updates in one filename order; the zz/zzzz prefixes rely on it.
    return base + sorted(updates, key=lambda p: p.name)


def rebuild(_args):
    mysql(f"DROP DATABASE IF EXISTS {COA_SCHEMA}; CREATE DATABASE {COA_SCHEMA} CHARACTER SET utf8mb4;")
    with zipfile.ZipFile(PACKAGE) as archive:
        for name in archive.namelist():
            body = archive.read(name)
            mysql(b"SET FOREIGN_KEY_CHECKS=0; SET UNIQUE_CHECKS=0; SET autocommit=0;\n" + body + b"\nCOMMIT;\n",
                  COA_SCHEMA, timeout=3600)
        print("package tables", len(archive.namelist()))
    files = ordered_sql("world")
    for path in files:
        try:
            mysql(path.read_bytes(), COA_SCHEMA, timeout=3600)
        except RuntimeError as error:
            raise SystemExit(f"{path.name}: {error}")
    # The endgame tables are seeded from manastorm_scaling_curve, which the zz_ snapshot/tuning files only fill later in
    # filename order (CoA's live DB got them in date order). Both files are idempotent, so run them again at the end.
    for name in ENDGAME_RESEED:
        mysql((COA / "data/sql/updates/pending_db_world" / name).read_bytes(), COA_SCHEMA)
    print("Manastorm world files applied", len(files))


# ------------------------------------------------------------------------------------------------------ stage
def cpp_numbers() -> list[int]:
    import re
    source = "".join(p.read_text(errors="replace") for p in (ESTERIA / "modules/mod-manastorm-endless/src").glob("*"))
    return sorted({int(x) for x in re.findall(r"(?<![\w.])(\d{4,8})(?![\w.])", source)})


def spell_closure(coa: dict[str, Table], est: dict[str, Table], seed_spells: set[int], seed_enchants: set[int]):
    """Spells and enchantments reachable from the seeds through triggers, enchant effects and enchant spells."""
    spells, enchants = set(), set()
    todo = [("s", s) for s in seed_spells] + [("e", e) for e in seed_enchants]
    while todo:
        kind, row_id = todo.pop()
        if kind == "s":
            row = coa["Spell"].rows.get(row_id)
            if not row or row_id in spells:
                continue
            spells.add(row_id)
            todo += [("s", row[f]) for f in SPELL_REFS if row[f]]
            todo += [("e", row[110 + i]) for i in range(3) if row[71 + i] in SPELL_ENCHANT_EFFECTS and row[110 + i]]
        else:
            row = coa["SpellItemEnchantment"].rows.get(row_id)
            if not row or row_id in enchants:
                continue
            enchants.add(row_id)
            todo += [("s", row[11 + i]) for i in range(3) if row[2 + i] in ENCHANT_SPELL_TYPES and row[11 + i]]
    return spells, enchants


def stage(_args):
    if STAGE.exists() and any(STAGE.iterdir()):
        raise SystemExit(f"{STAGE} is not empty; move it aside first")
    (STAGE / "server").mkdir(parents=True, exist_ok=True)
    cpp = ",".join(map(str, cpp_numbers()))
    mysql((HERE / "idset.sql").read_text(encoding="utf-8").replace("@CPP_NUMBERS@", cpp)
          .replace("@ITEM_REMAP_START@", str(ITEM_REMAP_START - 1))
          .replace("@CLIENT_ID_LIMIT@", str(CLIENT_ID_LIMIT)), COA_SCHEMA, timeout=3600)
    taken = column("SELECT COUNT(*) FROM ms_port_item p JOIN item_template c ON c.entry = p.new_entry WHERE p.entry <> p.new_entry")[0] + \
        column("SELECT COUNT(*) FROM ms_port_item p JOIN acore_world.item_template e ON e.entry = p.new_entry WHERE p.entry <> p.new_entry")[0]
    if taken:
        raise SystemExit(f"{taken} item remap targets are already used; move ITEM_REMAP_START")

    coa = {name: coa_table(name) for name in DBC}
    est = {name: esteria_table(name) for name in DBC}
    item_spells = column("SELECT DISTINCT s FROM (SELECT spellid_1 s, i.entry FROM item_template i UNION ALL "
                         "SELECT spellid_2, i.entry FROM item_template i UNION ALL SELECT spellid_3, i.entry FROM item_template i "
                         "UNION ALL SELECT spellid_4, i.entry FROM item_template i UNION ALL SELECT spellid_5, i.entry FROM item_template i) q "
                         "JOIN ms_port_item p ON p.entry = q.entry WHERE s > 0")
    named_spells = column("SELECT DISTINCT id FROM ms_ids WHERE kind = 'spell'")
    cpp_spells = [n for n in cpp_numbers() if n in coa["Spell"].rows and n not in est["Spell"].rows]
    sets = set(column("SELECT DISTINCT i.itemset FROM item_template i JOIN ms_port_item p ON p.entry = i.entry WHERE i.itemset > 0"))
    set_spells = {coa["ItemSet"].rows[s][35 + i] for s in sets if s in coa["ItemSet"].rows and s not in est["ItemSet"].rows
                  for i in range(8)} - {0}
    random_props = set(column("SELECT DISTINCT e.ench FROM item_enchantment_template e JOIN item_template i ON i.RandomProperty = e.entry "
                              "JOIN ms_port_item p ON p.entry = i.entry"))
    random_suffix = set(column("SELECT DISTINCT e.ench FROM item_enchantment_template e JOIN item_template i ON i.RandomSuffix = e.entry "
                               "JOIN ms_port_item p ON p.entry = i.entry"))
    enchant_seed = set(column("SELECT DISTINCT i.socketBonus FROM item_template i JOIN ms_port_item p ON p.entry = i.entry "
                              "WHERE i.socketBonus > 0"))
    enchant_seed |= {coa["ItemRandomProperties"].rows[r][2 + i] for r in random_props if r in coa["ItemRandomProperties"].rows
                     for i in range(5)}
    enchant_seed |= {coa["ItemRandomSuffix"].rows[r][19 + i] for r in random_suffix if r in coa["ItemRandomSuffix"].rows
                     for i in range(5)}
    spells, enchants = spell_closure(coa, est, set(item_spells) | set(named_spells) | set(cpp_spells) | set_spells,
                                     enchant_seed - {0})

    # Same ID on both sides: identical name = the same spell (keep Esteria's); different name = clash, gets a new ID.
    # A spell Esteria lacks keeps its ID unless that is past the client's table limit.
    spell_map, clashes = {}, []
    for spell in sorted(spells):
        if spell not in est["Spell"].rows:
            if spell <= CLIENT_ID_LIMIT:
                spell_map[spell] = spell
            else:
                clashes.append(spell)
        elif coa["Spell"].name_of(spell, 136) != est["Spell"].name_of(spell, 136):
            clashes.append(spell)
    used = set(est["Spell"].rows) | set(coa["Spell"].rows) | set(spell_map)
    next_id = SPELL_REMAP_START
    for spell in clashes:
        while next_id in used:
            next_id += 1
        spell_map[spell] = next_id
        used.add(next_id)
    # The module C++ must name remapped IDs, never the clashing CoA ones (AscensionManastormGadgets.h does this by hand).
    cpp_set = set(cpp_numbers())
    item_remaps = dict(zip(*[iter(column("SELECT entry, new_entry FROM ms_port_item WHERE entry <> new_entry"))] * 2))
    stale = sorted(a for a, b in list(spell_map.items()) + list(item_remaps.items()) if a != b and a in cpp_set)
    drifted = sorted(n for n in cpp_set if SPELL_REMAP_START <= n < SPELL_REMAP_START + 100000 and n not in spell_map.values())
    if stale or drifted:
        remaps = {a: spell_map.get(a, item_remaps.get(a)) for a in stale}
        raise SystemExit(f"module C++ names clashing IDs (old -> new) {remaps} / unknown remap IDs {drifted}")
    enchant_map = {e: e for e in enchants if e not in est["SpellItemEnchantment"].rows}
    mysql("DROP TABLE IF EXISTS ms_port_spell; CREATE TABLE ms_port_spell (id INT UNSIGNED PRIMARY KEY, new_id INT UNSIGNED NOT NULL);"
          + "".join(f"INSERT INTO ms_port_spell VALUES {','.join(f'({a},{b})' for a, b in chunk)};"
                    for chunk in chunks(sorted(spell_map.items()), 5000)), COA_SCHEMA)

    # Server DBC continuations: rows Esteria lacks, with spell references pointed at the remapped IDs.
    remap_spell = lambda v: spell_map.get(v, v)  # noqa: E731

    def spell_row(row):
        # Manastorm's own gadgets that grow per level stopped at 70 (CoA's cap is 60); Esteria's is 80. Item procs keep
        # their caps on purpose, like stock items.
        if coa["Spell"].name_of(row[0], 136).startswith("Manastorm:") and 0 < row[37] < 80 and any(row[77:80]):
            row[37] = 80
        row[0] = remap_spell(row[0])
        for field in SPELL_REFS:
            row[field] = remap_spell(row[field])
        return row

    def enchant_row(row):
        for i in range(3):
            if row[2 + i] in ENCHANT_SPELL_TYPES:
                row[11 + i] = remap_spell(row[11 + i])
        return row

    def set_row(row):
        for i in range(17):  # member items
            row[18 + i] = item_remaps.get(row[18 + i], row[18 + i])
        for i in range(8):
            row[35 + i] = remap_spell(row[35 + i])
        return row

    def cost_row(row):
        for i in range(5):  # required items
            row[4 + i] = item_remaps.get(row[4 + i], row[4 + i])
        return row

    def limit_row(row):
        row[0] = limit_map.get(row[0], row[0])
        return row

    ported = [s for s in spells if s in spell_map]
    lookups = {
        "SpellDuration": {coa["Spell"].rows[s][40] for s in ported},
        "SpellCastTimes": {coa["Spell"].rows[s][28] for s in ported},
        "SpellRange": {coa["Spell"].rows[s][46] for s in ported},
        "SpellRadius": {coa["Spell"].rows[s][92 + i] for s in ported for i in range(3)},
        "ItemSet": sets,
        "ItemLimitCategory": set(column("SELECT DISTINCT i.ItemLimitCategory FROM item_template i JOIN ms_port_item p "
                                        "ON p.entry = i.entry WHERE i.ItemLimitCategory > 0")),
        "ItemExtendedCost": set(column("SELECT DISTINCT v.ExtendedCost FROM npc_vendor v JOIN ms_ids m ON m.kind = 'creature' "
                                       "AND m.id = v.entry WHERE v.ExtendedCost > 0")),
        "ItemRandomProperties": random_props, "ItemRandomSuffix": random_suffix,
        "ScalingStatDistribution": set(column("SELECT DISTINCT i.ScalingStatDistribution FROM item_template i JOIN ms_port_item p "
                                              "ON p.entry = i.entry WHERE i.ScalingStatDistribution > 0")),
    }
    # Esteria's core reads item_template.ItemLimitCategory as int16 (ObjectMgr.cpp); CoA has a few IDs above that.
    free = (i for i in range(LIMIT_REMAP_START, 32768) if i not in coa["ItemLimitCategory"].rows
            and i not in est["ItemLimitCategory"].rows)
    limit_map = {i: next(free) for i in sorted(lookups["ItemLimitCategory"]) if i > 32767}
    mysql("DROP TABLE IF EXISTS ms_port_limit; CREATE TABLE ms_port_limit (id INT UNSIGNED PRIMARY KEY, new_id INT UNSIGNED NOT NULL);"
          + (f"INSERT INTO ms_port_limit VALUES {','.join(f'({a},{b})' for a, b in limit_map.items())};" if limit_map else ""),
          COA_SCHEMA)
    family_limits = unique_family_limits(coa["ItemLimitCategory"], free)
    lookups["ItemLimitCategory"] |= set(family_limits.values())
    report = {"spells": {"closure": len(spells), "ported": len(ported), "remapped": {str(a): b for a, b in spell_map.items() if a != b}},
              "item_limit_categories_remapped": {str(a): b for a, b in limit_map.items()},
              "enchantments": {"closure": len(enchants), "ported": len(enchant_map)}, "dbc": {}}
    outputs = {"Spell": (sorted(ported), spell_row), "SpellItemEnchantment": (sorted(enchant_map), enchant_row)}
    for name, ids in lookups.items():
        wanted = sorted(i for i in ids - {0} if i in coa[name].rows and i not in est[name].rows)
        report["dbc"][name] = {"used": len(ids - {0}), "added": len(wanted),
                               "kept_esteria": sum(1 for i in ids - {0} if i in est[name].rows),
                               "missing_everywhere": sorted(i for i in ids - {0} if i not in coa[name].rows and i not in est[name].rows)}
        outputs[name] = (wanted, {"ItemSet": set_row, "ItemLimitCategory": limit_row, "ItemExtendedCost": cost_row}.get(name, lambda row: row))
    for name, (ids, fix) in outputs.items():
        if ids:
            (STAGE / "server" / f"{name}.dbc1-{CONTINUATION}").write_bytes(coa[name].build(ids, fix))

    world, characters = build_sql(report)
    # the stock half of each unique family is Esteria's own row, which the stage never copies: tag it in place
    world += b"".join(f"UPDATE item_template SET ItemLimitCategory = {limit} WHERE entry = {base} AND ItemLimitCategory = 0;\n"
                      .encode() for base, limit in sorted(family_limits.items()))
    report["unique_families"] = len(family_limits)
    (STAGE / "world.sql").write_bytes(world)
    (STAGE / "characters.sql").write_bytes(characters)
    (STAGE / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "spells"} | {"spells": {k: (len(v) if isinstance(v, dict) else v)
                      for k, v in report["spells"].items()}}, indent=2))


COPY_OFFSETS = (4500000, 4600000)  # Manastorm +5/+10 item-level copies (zzzzz_esteria_wotlk_endgame_caches.sql)
ITEM_FLAG_UNIQUE_EQUIPPABLE = 0x80000


def unique_family_limits(limits: Table, free) -> dict[int, int]:
    """A copy and its stock item are separate entries, so "Unique-Equipped" (or one per player) alone lets a player wear
    both. Give each such family one equip limit category of quantity 1, named after the item, and tag the copies in
    COA_SCHEMA so the stage carries it. Returns {stock entry: category}. Families whose stock item already has a
    category share it through the copy, which was built from the stock row."""
    lo, hi = COPY_OFFSETS[0], COPY_OFFSETS[-1] + 99999
    rows = [line.split("\t", 1) for line in mysql(
        "SELECT e.entry, e.name FROM acore_world.item_template e "
        f"WHERE e.ItemLimitCategory = 0 AND (e.Flags & {ITEM_FLAG_UNIQUE_EQUIPPABLE} OR e.maxcount = 1) "
        f"AND e.entry IN (SELECT entry % 100000 FROM item_template WHERE entry BETWEEN {lo} AND {hi}) ORDER BY e.entry",
        COA_SCHEMA).splitlines() if line]
    sample = next(r for r in limits.rows.values() if r[19] == 1)  # an equip-limited row: keeps its locale flags field
    families = {}
    for (entry, name), limit in zip(rows, free):
        row = [0] * limits.fields
        row[0], row[17], row[18], row[19] = limit, sample[17], 1, 1  # quantity 1, mode equip
        limits.rows[limit] = row
        limits.text[limit, 1] = name
        families[int(entry)] = limit
    if len(families) < len(rows):
        raise SystemExit(f"out of free ItemLimitCategory IDs below 32768 ({len(rows) - len(families)} families left)")
    if families:
        mysql("".join(f"UPDATE item_template SET ItemLimitCategory = {limit} WHERE entry IN ({', '.join(str(o + base) for o in COPY_OFFSETS)});"
                      for base, limit in families.items()), COA_SCHEMA)
    return families


def chunks(values, size):
    for start in range(0, len(values), size):
        yield values[start:start + size]


def build_sql(report) -> tuple[bytes, bytes]:
    mysql((HERE / "stage_rows.sql").read_text(encoding="utf-8"), COA_SCHEMA, timeout=3600)
    manastorm = mysql(f"SELECT table_name FROM information_schema.tables WHERE table_schema = '{STAGE_SCHEMA}' "
                      "AND table_name LIKE 'manastorm%' ORDER BY table_name").split()
    shared = mysql(f"SELECT table_name FROM information_schema.tables WHERE table_schema = '{STAGE_SCHEMA}' "
                   "AND table_name NOT LIKE 'manastorm%' ORDER BY table_name").split()
    report["rows"] = {t: int(mysql(f"SELECT COUNT(*) FROM `{t}`", STAGE_SCHEMA)) for t in manastorm + shared}
    header = (b"-- Generated by wowdecomp Tools/ManastormPort/manastorm_port.py from CoA mod-manastorm-endless.\n"
              b"-- Manastorm tables are created fresh; shared rows use INSERT IGNORE so Esteria's own rows always win.\n"
              b"SET FOREIGN_KEY_CHECKS=0;\n"
              # 36 Ascension descriptions run past Esteria's 255 (max 423); CoA's column is 1024 and the core reads a string
              b"ALTER TABLE item_template MODIFY description VARCHAR(1024) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '';\n")
    world = header + dump(STAGE_SCHEMA, manastorm) + dump(STAGE_SCHEMA, shared, "--no-create-info", "--insert-ignore")
    characters = b"".join(b"-- " + p.name.encode() + b"\n" + p.read_bytes() + b"\n" for p in ordered_sql("characters"))
    return world, characters


# ------------------------------------------------------------------------------------------------------- test
def test(_args):
    for source, target in (("acore_world", "ms_test_world"), ("acore_characters", "ms_test_characters")):
        mysql(f"DROP DATABASE IF EXISTS {target}; CREATE DATABASE {target} CHARACTER SET utf8mb4;")
        subprocess.run(["docker", "exec", "ac-database", "sh", "-c",
                        f'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysqldump -uroot --single-transaction --skip-lock-tables '
                        f'--routines --triggers {source} | MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot {target}'],
                       check=True)
    # Esteria-wide checks also count Esteria's own gaps, so show each count before and after the stage.
    checks = (HERE / "verify.sql").read_text(encoding="utf-8")
    manastorm_free = checks.split("\nSELECT")[0] + "".join("\nSELECT" + q for q in checks.split("\nSELECT")[1:] if "manastorm_" not in q)
    before = dict(line.split("\t") for line in mysql(manastorm_free, "ms_test_world").splitlines())
    mysql((STAGE / "world.sql").read_bytes(), "ms_test_world", timeout=3600)
    mysql((STAGE / "characters.sql").read_bytes(), "ms_test_characters", timeout=3600)
    for line in mysql(checks, "ms_test_world").splitlines():
        name, count = line.split("\t")
        print(f"{name}: {before.get(name, '-')} -> {count}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("rebuild", "stage", "test"))
    args = parser.parse_args()
    {"rebuild": rebuild, "stage": stage, "test": test}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
