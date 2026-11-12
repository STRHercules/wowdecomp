"""Client side of the Manastorm port: the rows and files Esteria's client needs for what manastorm_port.py staged.

    python -I manastorm_client.py stage     # -> G:/ManastormPort/client: tables/*.dbc deltas, assets.tsv, report.json;
                                            #    also moves display IDs in the server stage (ms_port_display) and
                                            #    rewrites G:/ManastormPort/stage/world.sql to match
    python -I manastorm_client.py pack      # -> G:/ManastormPort/client/patch-Manastorm.MPQ (files copied out of CoA)
    python -I manastorm_client.py install --client "G:/3.3.5a - Dev"   # registry append + MPQ, with a backup first

Run after `manastorm_port.py stage`. Rules, as in the Eclipse import (docs/eclipse-import.md): Esteria's rows and files
win; a row the client already has under the same ID with the same content is reused; a CoA row whose ID means something
else in Esteria's client (Ascension renumbered stock item displays) gets a new ID; references to tables we don't port
(sounds, item visuals, particle colours...) are kept only when Esteria's client has that ID, else zeroed.
Client rows go through the supplemental registry (Engine/Source/Retail/ClientTables.cpp), so every ID stays <= 5,000,000.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import struct
import sys
import time
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "EclipseImport"))
import eclipse_data as ed  # noqa: E402  (also puts RetailAssets and Esteria's tools on sys.path)
import eclipse_install as ei  # noqa: E402
import manastorm_port as mp  # noqa: E402
import native_import as n  # noqa: E402

COA_CLIENT = Path(r"G:\CoAClient\Client")
OUT = mp.WORK / "client"
ARCHIVE = "patch-Manastorm.MPQ"
LIMIT = mp.CLIENT_ID_LIMIT
LOC = lambda first: tuple(range(first, first + 16))  # noqa: E731
# Client tables written here: field count and string columns (must match ClientTables.cpp's layouts).
LAYOUT = {
    "Item": (8, ()), "ItemDisplayInfo": (25, (1, 2, 3, 4, 5, 6) + tuple(range(15, 23))),
    "Spell": (234, LOC(136) + LOC(153) + LOC(170) + LOC(187)), "SpellIcon": (2, (1,)), "SpellCategory": (2, ()),
    "SpellVisual": (32, ()), "SpellVisualKit": (38, ()), "SpellVisualEffectName": (7, (1, 2)),
    "SpellVisualKitModelAttach": (10, ()),
    "CreatureDisplayInfo": (16, (6, 7, 8, 9)), "CreatureModelData": (28, (2,)), "GameObjectDisplayInfo": (19, (1,)),
    "SpellDuration": (4, ()), "SpellRadius": (4, ()), "SpellRange": (40, LOC(6) + LOC(23)),
    "ItemSet": (53, LOC(1)), "ItemLimitCategory": (20, LOC(1)), "ItemExtendedCost": (16, ()),
    "SpellItemEnchantment": (38, LOC(14)), "ScalingStatDistribution": (22, ()),
}
# First IDs for rows that need a new one (free in Esteria's client, its registry and CoA at the time of writing;
# the allocator still skips anything taken).
NEW_ID = {"ItemDisplayInfo": 3500001, "SpellIcon": 3500001, "SpellVisual": 3500001, "SpellVisualKit": 3500001,
          "SpellVisualEffectName": 3500001, "SpellVisualKitModelAttach": 3500001, "CreatureDisplayInfo": 3500001,
          "CreatureModelData": 3500001, "GameObjectDisplayInfo": 3500001}
ICON_FALLBACK = "Interface\\Icons\\INV_Misc_QuestionMark"
OBJECT_DIRS = {1: "head", 3: "shoulder", 14: "shield", 16: "cape", 23: "weapon", 27: "quiver"}
BODY_DIRS = ("armuppertexture", "armlowertexture", "handtexture", "torsouppertexture", "torsolowertexture",
             "leguppertexture", "leglowertexture", "foottexture")


def canon(path: str) -> str:
    """A file name as the client resolves it: case-insensitive, and .mdx/.mdl load as .m2."""
    return ed.as_m2(path.replace("/", "\\")).lower()


# ------------------------------------------------------------------------------------------------- client views
class Rows:
    """One table as a client sees it: {id: (row, strings)} with string columns decoded."""

    def __init__(self, name: str):
        self.name, (self.fields, self.strings) = name, LAYOUT.get(name, (0, ()))
        self.rows: dict[int, tuple[list[int], dict[int, str]]] = {}

    def add(self, row: list[int], pool: bytes):
        self.rows[row[0]] = (list(row), {c: ed.cstr(pool, row[c]) for c in self.strings})

    def text(self, row_id: int, column: int) -> str:
        return self.rows[row_id][1].get(column, "") if row_id in self.rows else ""

    def __contains__(self, row_id: int) -> bool:
        return row_id in self.rows


class Client:
    """A client folder's archives (plus, for Esteria's, the active supplemental registry)."""

    def __init__(self, root: Path, registry: bool):
        self.archives = ed.Archives(root)
        self.cache: dict[str, Rows] = {}
        self.registry = None
        if registry:
            config = dict(line.strip().split("=", 1) for line in (root / "Data/Retail/RetailCasc.ini").read_text().splitlines()
                          if "=" in line)
            self.registry = root / "Data/Retail" / config["tables"]
        self._where = None

    def table(self, name: str) -> Rows:
        if name not in self.cache:
            rows = Rows(name)
            for row, pool in self.archives.tables(name).values():
                rows.add(row, pool)
            path = self.registry / f"{name.lower()}.dbc" if self.registry else None
            if path and path.is_file():
                registry_rows, pool = n.wdbc(path.read_bytes(), rows.fields)
                for row in registry_rows:
                    rows.add(row, pool)
            self.cache[name] = rows
        return self.cache[name]

    def ids(self, name: str) -> set[int]:
        return set(self.table(name).rows)

    def has_file(self, name: str) -> bool:
        return ed.norm(name) in self.archives.names() or self.archives.has(name)

    def where(self) -> dict[str, Path]:
        """Listed file -> the archive that serves it (later names win)."""
        if self._where is None:
            self._where = {}
            for path in self.archives.paths:  # sorted by name, so later archives overwrite
                for listed in self.archives.listed(path):
                    self._where[ed.norm(listed)] = path
        return self._where

    def read(self, name: str) -> bytes:
        path = self.where()[ed.norm(name)]
        return self.archives.storm.read(self.archives.handles[path], name.replace("/", "\\"))


# ------------------------------------------------------------------------------------------------------- stage
class Stage:
    def __init__(self, dev: Client, coa: Client):
        self.dev, self.coa = dev, coa
        self.out: dict[str, dict[int, tuple[list[int], dict[int, str]]]] = {name: {} for name in LAYOUT}
        self.taken: dict[str, set[int]] = {}
        self.files: set[str] = set()          # normalized CoA paths to ship
        self.missing_files: Counter = Counter()
        self.stats: Counter = Counter()

    # IDs ---------------------------------------------------------------------------------------------------------
    def used(self, name: str) -> set[int]:
        if name not in self.taken:
            self.taken[name] = self.dev.ids(name) | set(self.out[name])
        return self.taken[name]

    def allocate(self, name: str) -> int:
        used, start = self.used(name), NEW_ID[name]
        coa = self.coa.ids(name)
        while start in used or start in coa:
            start += 1
        if start > LIMIT:
            raise SystemExit(f"{name}: client ID budget exhausted")
        used.add(start)
        return start

    def emit(self, name: str, row: list[int], strings: dict[int, str]):
        if row[0] > LIMIT or row[0] in self.dev.table(name) or row[0] in self.out[name] and self.out[name][row[0]][0] != row:
            raise ValueError(f"{name} {row[0]}: cannot add (taken or past the client limit)")
        self.out[name][row[0]] = (row, strings)
        self.used(name).add(row[0])

    def keep_ref(self, table: str, value: int) -> int:
        """A reference into a table we don't port: kept when Esteria's client has that row."""
        return value if not value or value in self.dev.table(table) else 0

    # files -------------------------------------------------------------------------------------------------------
    def need_file(self, name: str) -> bool:
        """True when the client will have the file (Esteria's, or shipped from CoA)."""
        key = ed.norm(name)
        if not key or self.dev.has_file(key):
            return True
        if key in self.coa.where():
            if key not in self.files:
                self.files.add(key)
                if key.endswith(".m2"):
                    self.model_closure(key)
            return True
        self.missing_files[key.rsplit("\\", 1)[0]] += 1
        return False

    def model_closure(self, key: str):
        data = self.coa.read(key)
        if data[:4] != b"MD20":
            return
        for dependency in ed.m2_skins(key, data) + ed.m2_external_anims(key, data) + ed.m2_textures(data):
            self.need_file(dependency)

    def need_prefix(self, prefix: str) -> bool:
        """Item components are named by stem; the client picks the suffixed file. Ship every CoA file under the stem."""
        prefix = ed.norm(prefix)
        if any(name.startswith(prefix) for name in self.dev_names_under(prefix)):
            return True
        found = [name for name in self.coa_names_under(prefix)]
        for name in found:
            self.need_file(name)
        if not found:
            self.missing_files[prefix.rsplit("\\", 1)[0]] += 1
        return bool(found)

    def dev_names_under(self, prefix):
        return self._under("_dev_sorted", lambda: sorted(self.dev.archives.names()), prefix)

    def coa_names_under(self, prefix):
        return self._under("_coa_sorted", lambda: sorted(self.coa.where()), prefix)

    def _under(self, attr, build, prefix):
        import bisect
        names = getattr(self, attr, None)
        if names is None:
            names = build()
            setattr(self, attr, names)
        i = bisect.bisect_left(names, prefix)
        while i < len(names) and names[i].startswith(prefix):
            yield names[i]
            i += 1

    # item displays -------------------------------------------------------------------------------------------------
    @staticmethod
    def look_key(row, strings):
        # models, model textures and the inventory icon (5): items without a model (caches, tokens) differ only by icon
        return (tuple(strings[c].lower().rsplit(".", 1)[0] for c in (1, 2, 3, 4, 5)),
                tuple(strings[c].lower() for c in range(15, 23)), tuple(row[7:11]), tuple(row[13:15]))

    def item_displays(self, displays: dict[int, int]) -> dict[int, int]:
        """CoA display -> client display. displays: CoA display -> an inventory type that uses it."""
        dev, coa = self.dev.table("ItemDisplayInfo"), self.coa.table("ItemDisplayInfo")
        looks = {}
        for row_id in sorted(dev.rows, reverse=True):
            row, strings = dev.rows[row_id]
            looks[self.look_key(row, strings)] = row_id
        mapping = {}
        for display, inventory in sorted(displays.items()):
            if display not in coa:
                self.stats["item display missing in CoA"] += 1
                mapping[display] = 0
                continue
            row, strings = coa.rows[display]
            key = self.look_key(row, strings)
            if key in looks:
                mapping[display] = looks[key]
                self.stats["item display reused"] += 1
                continue
            new_id = display if display not in self.used("ItemDisplayInfo") and display not in self.dev.table("ItemDisplayInfo") \
                else self.allocate("ItemDisplayInfo")
            row, strings = list(row), dict(strings)
            row[0] = new_id
            row[11] = row[23] = row[24] = 0  # realm spell visual, item visual, particle colour (renumbered by Ascension)
            row[12] = self.keep_ref("ItemGroupSounds", row[12])
            row[13] = self.keep_ref("HelmetGeosetVisData", row[13])
            row[14] = self.keep_ref("HelmetGeosetVisData", row[14])
            strings[5] = self.icon_name(strings[5])
            strings[6] = ""
            folder = OBJECT_DIRS.get(inventory, "weapon" if inventory in (13, 15, 17, 21, 22, 25, 26) else None)
            drawn = True
            for c in (1, 2, 3, 4):
                stem = strings[c].lower().rsplit(".", 1)[0] if c < 3 else strings[c].lower()
                if stem:
                    drawn &= bool(folder) and self.need_prefix(f"item\\objectcomponents\\{folder}\\{stem}")
            for region, c in zip(BODY_DIRS, range(15, 23)):
                if strings[c]:
                    drawn &= self.need_prefix(f"item\\texturecomponents\\{region}\\{strings[c].lower()}")
            self.stats["item display added" if drawn else "item display added (some files missing)"] += 1
            self.emit("ItemDisplayInfo", row, strings)
            looks[key] = new_id
            mapping[display] = new_id
        return mapping

    def icon_name(self, icon: str) -> str:
        """An inventory icon stem (ItemDisplayInfo) that the client can draw."""
        icon = re.sub(r"\.blp$", "", icon, flags=re.I)
        if icon and self.need_file(f"Interface\\Icons\\{icon}.blp"):
            return icon
        return "INV_Misc_QuestionMark"

    # spells ----------------------------------------------------------------------------------------------------------
    def spell_icon(self, icon: int) -> int:
        if not icon:
            return 0
        if icon in self.out["SpellIcon"]:
            return icon
        path = self.coa.table("SpellIcon").text(icon, 1)
        if not path:
            return 0
        dev = self.dev.table("SpellIcon")
        if dev.text(icon, 1).lower() == path.lower():
            return icon
        for row_id, (_, strings) in dev.rows.items():
            if strings[1].lower() == path.lower():
                return row_id
        if not self.need_file(path + ".blp"):
            path = ICON_FALLBACK
        new_id = icon if icon not in self.used("SpellIcon") else self.allocate("SpellIcon")
        self.emit("SpellIcon", [new_id, 0], {1: path})
        return new_id

    def same(self, name: str, row_id: int, row: list[int], strings: dict[int, str]) -> bool:
        dev = self.dev.table(name)
        if row_id not in dev:
            return False
        theirs, their_strings = dev.rows[row_id]
        return all(theirs[c] == row[c] for c in range(1, len(row)) if c not in strings) and \
            all(canon(their_strings[c]) == canon(v) for c, v in strings.items())

    def place(self, name: str, source_id: int, row: list[int], strings: dict[int, str], memo: dict) -> int:
        """Reuse Esteria's identical row, else add under the source ID if free, else under a new ID."""
        if self.same(name, source_id, row, strings):
            memo[source_id] = source_id
            return source_id
        new_id = source_id if source_id not in self.used(name) else self.allocate(name)
        memo[source_id] = new_id
        row = list(row)
        row[0] = new_id
        self.emit(name, row, strings)
        return new_id

    def effect(self, effect: int, memo={}) -> int:
        if not effect or effect in memo:
            return memo.get(effect, 0)
        coa = self.coa.table("SpellVisualEffectName")
        if effect not in coa:
            return 0
        row, strings = coa.rows[effect]
        if strings[2] and not self.need_file(ed.as_m2(strings[2])):
            self.stats["visual effect model missing"] += 1
        return self.place("SpellVisualEffectName", effect, row, strings, memo)

    def kit(self, kit: int, memo={}) -> int:
        if not kit or kit in memo:
            return memo.get(kit, 0)
        coa = self.coa.table("SpellVisualKit")
        if kit not in coa:
            return 0
        row = list(coa.rows[kit][0])
        for c in range(3, 15):  # head .. world effects
            row[c] = self.effect(row[c])
        row[15] = self.keep_ref("SoundEntries", row[15])
        row[16] = self.keep_ref("SpellEffectCameraShakes", row[16])
        new_id = self.place("SpellVisualKit", kit, row, {}, memo)
        for attach_id, (attach, _) in self.coa.table("SpellVisualKitModelAttach").rows.items():
            if attach[1] == kit:
                attach = list(attach)
                attach[1], attach[2] = new_id, self.effect(attach[2])
                self.place("SpellVisualKitModelAttach", attach_id, attach, {}, {})
        return new_id

    def visual(self, visual: int, memo={}) -> int:
        if not visual or visual in memo:
            return memo.get(visual, 0)
        coa = self.coa.table("SpellVisual")
        if visual not in coa:
            return 0
        row = list(coa.rows[visual][0])
        for c in (1, 2, 3, 4, 5, 6, 14, 15, 22, 23, 24, 25):
            row[c] = self.kit(row[c])
        row[8] = self.effect(row[8])
        row[11] = self.keep_ref("SoundEntries", row[11])
        row[12] = self.keep_ref("SoundEntries", row[12])
        row[21] = self.keep_ref("SpellMissileMotion", row[21])
        return self.place("SpellVisual", visual, row, {}, memo)

    def spells(self, table: mp.Table):
        """Client rows for the staged server spells (IDs and trigger references already remapped)."""
        refs = {18: "SpellFocusObject", 222: "TotemCategory", 223: "TotemCategory", 224: "AreaGroup",
                226: "SpellRuneCost", 227: "SpellMissile", 228: "PowerDisplay", 232: "SpellDescriptionVariables",
                233: "SpellDifficulty"}
        for spell_id, row in sorted(table.rows.items()):
            if spell_id in self.dev.table("Spell"):
                self.stats["spell already in client"] += 1
                continue
            row = list(row)
            row[1] = self.category(row[1])
            for c, ref in refs.items():
                before = row[c]
                row[c] = self.keep_ref(ref, row[c])
                if before != row[c]:
                    self.stats[f"spell {ref} reference zeroed"] += 1
            row[131], row[132] = self.visual(row[131]), self.visual(row[132])
            row[133], row[134] = self.spell_icon(row[133]), self.spell_icon(row[134])
            strings = {c: table.text.get((spell_id, c), "") for c in LAYOUT["Spell"][1]}
            self.emit("Spell", row, {c: v for c, v in strings.items() if v})

    def category(self, category: int) -> int:
        """SpellCategory (shared cooldowns): Esteria's row when it has the ID, else CoA's."""
        if not category or category in self.dev.table("SpellCategory") or category in self.out["SpellCategory"]:
            return category
        if category not in self.coa.table("SpellCategory"):
            return 0
        self.emit("SpellCategory", list(self.coa.table("SpellCategory").rows[category][0]), {})
        return category

    # creatures and gameobjects ---------------------------------------------------------------------------------------
    def creature_model(self, model: int, memo={}) -> int:
        if not model or model in memo:
            return memo.get(model, 0)
        row, strings = self.coa.table("CreatureModelData").rows[model]
        row, strings = list(row), {2: strings[2]}
        for c, ref in ((5, "UnitBlood"), (6, "FootprintTextures"), (13, "CreatureSoundData")):
            row[c] = self.keep_ref(ref, row[c])
        dev = self.dev.table("CreatureModelData")
        for row_id, (_, their) in dev.rows.items():
            if canon(their[2]) == canon(strings[2]):
                memo[model] = row_id
                return row_id
        self.need_file(ed.as_m2(strings[2]))
        return self.place("CreatureModelData", model, row, strings, memo)

    def creature_display(self, display: int) -> int:
        coa = self.coa.table("CreatureDisplayInfo")
        if display not in coa:
            self.stats["creature display missing in CoA"] += 1
            return display
        row, strings = coa.rows[display]
        row = list(row)
        model_path = self.coa.table("CreatureModelData").text(row[1], 2)
        row[1] = self.creature_model(row[1])
        row[3] = 0 if row[3] and row[3] not in self.dev.table("CreatureDisplayInfoExtra") else row[3]
        for c, ref in ((2, "CreatureSoundData"), (11, "UnitBlood"), (12, "NPCSounds"), (13, "ParticleColor")):
            row[c] = self.keep_ref(ref, row[c])
        folder = ed.norm(model_path).rsplit("\\", 1)[0]
        for c in (6, 7, 8):
            if strings[c]:
                self.need_file(f"{folder}\\{strings[c]}.blp")
        if strings[9]:
            self.need_file(f"{folder}\\{strings[9]}.blp")
        return self.place("CreatureDisplayInfo", display, row, dict(strings), {})

    def gameobject_display(self, display: int) -> int:
        coa = self.coa.table("GameObjectDisplayInfo")
        if display not in coa:
            self.stats["gameobject display missing in CoA"] += 1
            return display
        row, strings = coa.rows[display]
        row = list(row)
        for c in range(2, 12):
            row[c] = self.keep_ref("SoundEntries", row[c])
        model = strings[1]
        if model.lower().endswith(".wmo"):
            self.need_file(model)
            data = self.coa.read(model) if ed.norm(model) in self.coa.where() else b""
            groups = struct.unpack_from("<I", data, data.find(b"DHOM") + 8 + 4)[0] if b"DHOM" in data else 0
            for g in range(groups):
                self.need_file(model[:-4] + f"_{g:03d}.wmo")
        else:
            self.need_file(ed.as_m2(model))
        return self.place("GameObjectDisplayInfo", display, row, {1: model}, {})

    # lookup tables -----------------------------------------------------------------------------------------------------
    def lookups(self, wanted: dict[str, set[int]]):
        """Rows the client lacks for item/spell lookups: Esteria's server copy wins, then the staged continuation."""
        for name, ids in wanted.items():
            fields, strings = LAYOUT[name]
            server = mp.esteria_table(name) if (mp.EST_DBC / f"{name}.dbc").exists() else None
            staged = mp.STAGE / "server" / f"{name}.dbc1-{mp.CONTINUATION}"
            staged_table = mp.Table(name).add_file(staged) if staged.exists() else None
            coa = self.coa.table(name)
            for row_id in sorted(ids - {0} - self.dev.ids(name)):
                for source in (server, staged_table):
                    if source and row_id in source.rows:
                        row = list(source.rows[row_id])
                        text = {c: source.text.get((row_id, c), "") for c in strings}
                        break
                else:
                    if row_id not in coa:
                        self.stats[f"{name} missing everywhere"] += 1
                        continue
                    row, text = list(coa.rows[row_id][0]), dict(coa.rows[row_id][1])
                if name == "SpellItemEnchantment":
                    row[31] = self.keep_ref("ItemVisuals", row[31])
                    row[34] = self.keep_ref("SpellItemEnchantmentCondition", row[34])
                self.emit(name, row, {c: v for c, v in text.items() if v})


def server_ids(name: str) -> set[int]:
    """IDs the running worldserver has for a DBC: its base file plus every continuation except ours (read from the
    container, as the server reads them)."""
    import subprocess
    root = "/azerothcore/env/dist/data"
    listing = subprocess.run(["docker", "exec", "ac-worldserver", "sh", "-c", f"ls {root}/dbc-continuations"],
                             capture_output=True, text=True, check=True).stdout.split()
    paths = [f"{root}/dbc/{name}.dbc"] + [f"{root}/dbc-continuations/{f}" for f in listing
                                          if f.lower().startswith(name.lower() + ".dbc1-") and not f.endswith("-" + mp.CONTINUATION)]
    ids = set()
    for path in paths:
        data = subprocess.run(["docker", "exec", "ac-worldserver", "cat", path], capture_output=True, check=True).stdout
        _, count, _, size, _ = struct.unpack_from("<4s4I", data)
        ids |= {struct.unpack_from("<I", data, 20 + i * size)[0] for i in range(count)}
    sql = {"CreatureDisplayInfo": "creaturedisplayinfo_dbc", "CreatureModelData": "creaturemodeldata_dbc",
           "GameObjectDisplayInfo": "gameobjectdisplayinfo_dbc", "SpellCategory": "spellcategory_dbc"}.get(name)
    if sql:
        ids |= {int(v) for v in mp.mysql(f"SELECT ID FROM {sql}", "acore_world").split()}
    return ids


ADDON_DATA = HERE.parents[1] / "Addons/EsteriaManastorm/EsteriaManastormData.lua"
GADGETS_H = mp.ESTERIA / "modules/mod-manastorm-endless/src/AscensionManastormGadgets.h"
ART = "Interface\\Manastorm\\ManastormAtlas.blp"  # the portal scene the window shows (0..788 x 0..512 of 1024x512)


def lua_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\r", "").replace("\n", "\\n") + '"'


def write_addon_data(work: Stage, query):
    """Addons/EsteriaManastorm/EsteriaManastormData.lua: what the window needs that the server does not send."""
    gadgets = re.findall(r"\{(\d+), (\d+), (\d+), (\d+), (\d+), (\d+), (true|false)\}", GADGETS_H.read_text())
    if len(gadgets) != int(re.search(r"std::array<Gadget, (\d+)>", GADGETS_H.read_text()).group(1)):
        raise SystemExit("could not read every gadget from AscensionManastormGadgets.h")
    maps = {row[0]: ed.cstr(pool, row[5]) for row, pool in work.dev.archives.tables("Map").values()}
    segments = query("SELECT s.segment_id, s.map_id, COALESCE(c.name, '') FROM manastorm_segment s LEFT JOIN creature_template c "
                     "ON c.entry = IF(s.runtime_objective_creature_entry, s.runtime_objective_creature_entry, s.objective_creature_entry) "
                     "WHERE s.enabled ORDER BY s.segment_id")
    unlocks = query("SELECT t.track, t.trigger_depth, u.completion_message_id FROM manastorm_unlock_trigger t "
                    "JOIN manastorm_unlock u USING (unlock_id) WHERE t.enabled AND u.enabled AND u.completion_message_id "
                    "ORDER BY t.track, t.trigger_depth, u.completion_message_id")
    messages = {int(m): (icon, title, text) for m, icon, title, text in
                query("SELECT message_id, icon, title, REPLACE(REPLACE(description, '\\t', ' '), '\\n', ' ') FROM manastorm_unlock_message")}
    used = {int(m) for _, _, m in unlocks}
    lua = ["-- Generated by wowdecomp Tools/ManastormPort/manastorm_client.py (stage); do not edit.",
           "EsteriaManastormData = {",
           "    -- item, spell, previous rank's spell, family, rank, cost, passive (server: AscensionManastormGadgets.h)",
           "    gadgets = {"]
    lua += [f"        {{ {g[0]}, {g[1]}, {g[2]}, {g[3]}, {g[4]}, {g[5]}, {g[6]} }}," for g in gadgets]
    lua += ["    },", "    -- segment (the run's scene) = { map name, objective }", "    segments = {"]
    lua += [f"        [{s}] = {{ {lua_string(maps.get(int(m), ''))}, {lua_string(boss)} }}," for s, m, boss in segments]
    lua += ["    },", "    -- [track][depth] = { message, ... } shown when that depth is cleared (track 1 normal, 2 endgame)",
            "    unlocks = {"]
    for track in sorted({t for t, _, _ in unlocks}):
        lua.append(f"        [{track}] = {{")
        depths = {}
        for t, depth, message in unlocks:
            if t == track:
                depths.setdefault(int(depth), []).append(message)
        lua += [f"            [{d}] = {{ {', '.join(ms)} }}," for d, ms in sorted(depths.items())]
        lua.append("        },")
    lua += ["    },", "    -- message = { icon, title, text }", "    messages = {"]
    for m in sorted(used):
        icon, title, text = messages.get(m, ("", "", ""))
        icon = icon if icon and work.need_file(f"Interface\\Icons\\{icon}.blp") else "INV_Misc_QuestionMark"
        lua.append(f"        [{m}] = {{ {lua_string(icon)}, {lua_string(title)}, {lua_string(text)} }},")
    lua += ["    },", "}", ""]
    if not work.need_file(ART):
        raise SystemExit(f"{ART} is missing from CoA's archives")
    ADDON_DATA.parent.mkdir(parents=True, exist_ok=True)
    ADDON_DATA.write_text("\n".join(lua), encoding="utf-8")


def stage(_args):
    if OUT.exists() and any(OUT.iterdir()):
        raise SystemExit(f"{OUT} is not empty; move it aside first")
    (OUT / "tables").mkdir(parents=True, exist_ok=True)
    report_path = mp.STAGE / "report.json"
    server_report = json.loads(report_path.read_text(encoding="utf-8"))
    dev, coa = Client(ed.CLIENT, registry=True), Client(COA_CLIENT, registry=False)
    work = Stage(dev, coa)
    query = lambda sql: [line.split("\t") for line in mp.mysql(sql, mp.COA_SCHEMA).splitlines() if line]  # noqa: E731

    # items: CoA's own display IDs (before any earlier display remap), keyed by the item ID the stage gives them
    items = query("SELECT p.new_entry, c.class, c.subclass, c.SoundOverrideSubclass, c.Material, c.displayid, "
                  "c.InventoryType, c.sheath FROM ms_port_item p JOIN item_template c ON c.entry = p.entry")
    displays = {}
    for item in items:
        if int(item[5]):
            displays.setdefault(int(item[5]), int(item[6]))
    display_map = work.item_displays(displays)
    for item in items:
        entry, cls, sub, sound, material, display, inventory, sheath = (int(v) for v in item)
        work.emit("Item", [entry, cls, sub, sound, material, display_map.get(display, 0), inventory, sheath], {})

    # spells (the staged server rows), then the lookups they and the items use
    spells = mp.Table("Spell").add_file(mp.STAGE / "server" / f"Spell.dbc1-{mp.CONTINUATION}")
    work.spells(spells)
    stage_ids = lambda sql: {int(v) for v in mp.mysql(sql, mp.STAGE_SCHEMA).split()}  # noqa: E731
    wanted = {
        "SpellDuration": {r[40] for r in spells.rows.values()}, "SpellRange": {r[46] for r in spells.rows.values()},
        "SpellRadius": {r[92 + i] for r in spells.rows.values() for i in range(3)},
        "ItemSet": stage_ids("SELECT DISTINCT itemset FROM item_template"),
        "ItemLimitCategory": stage_ids("SELECT DISTINCT ItemLimitCategory FROM item_template"),
        "ItemExtendedCost": stage_ids("SELECT DISTINCT ExtendedCost FROM npc_vendor"),
        "ScalingStatDistribution": stage_ids("SELECT DISTINCT ScalingStatDistribution FROM item_template"),
        "SpellItemEnchantment": stage_ids("SELECT DISTINCT socketBonus FROM item_template"),
    }
    staged_enchants = mp.STAGE / "server" / f"SpellItemEnchantment.dbc1-{mp.CONTINUATION}"
    if staged_enchants.exists():
        wanted["SpellItemEnchantment"] |= set(mp.Table("SpellItemEnchantment").add_file(staged_enchants).rows)
    work.lookups(wanted)

    # creatures and gameobjects Esteria lacks (CoA's display IDs, as in the CoA world tables)
    moved = {"item": {a: b for a, b in display_map.items() if a != b}, "creature": {}, "gameobject": {}}
    for (display,) in query("SELECT DISTINCT m.CreatureDisplayID FROM creature_template_model m "
                            "JOIN ms_new_creature n ON n.entry = m.CreatureID"):
        new_id = work.creature_display(int(display))
        if new_id != int(display):
            moved["creature"][int(display)] = new_id
    for (display,) in query("SELECT DISTINCT g.displayId FROM gameobject_template g JOIN ms_new_go n USING (entry) "
                            "WHERE g.displayId > 0"):
        new_id = work.gameobject_display(int(display))
        if new_id != int(display):
            moved["gameobject"][int(display)] = new_id

    # write the client deltas
    counts = {}
    for name, rows in work.out.items():
        if not rows:
            continue
        fields, _ = LAYOUT[name]
        ordered = [rows[i] for i in sorted(rows)]
        strings = {(index, c): v for index, (_, text) in enumerate(ordered) for c, v in text.items() if v}
        data = n.make_wdbc(fields, [row for row, _ in ordered], strings)
        n.wdbc(data, fields)  # self-check: the registry's reader accepts it
        (OUT / "tables" / f"{name.lower()}.dbc").write_bytes(data)
        counts[name] = len(rows)
    write_addon_data(work, query)

    # the server validates creature/gameobject displays (and keeps spell categories): give it the rows it lacks
    server_rows = {}
    for name in ("CreatureDisplayInfo", "CreatureModelData", "GameObjectDisplayInfo", "SpellCategory"):
        rows = work.out[name]
        missing = sorted(set(rows) - server_ids(name))
        if missing:
            fields, _ = LAYOUT[name]
            ordered = [rows[i] for i in missing]
            strings = {(index, c): v for index, (_, text) in enumerate(ordered) for c, v in text.items() if v}
            (mp.STAGE / "server" / f"{name}.dbc1-{mp.CONTINUATION}").write_bytes(
                n.make_wdbc(fields, [row for row, _ in ordered], strings))
            server_rows[name] = len(missing)
    assets = sorted(work.files)
    (OUT / "assets.tsv").write_text("".join(f"{coa.where()[a].name}\t{a}\n" for a in assets), encoding="latin-1")

    # point the server stage at the moved display IDs and rewrite world.sql
    values = [f"('{kind}',{a},{b})" for kind, pairs in moved.items() for a, b in pairs.items()]
    mp.mysql("DROP TABLE IF EXISTS ms_port_display; CREATE TABLE ms_port_display (kind VARCHAR(16) NOT NULL, "
             "id INT UNSIGNED NOT NULL, new_id INT UNSIGNED NOT NULL, PRIMARY KEY (kind, id));"
             + "".join(f"INSERT INTO ms_port_display VALUES {','.join(chunk)};" for chunk in mp.chunks(values, 5000)),
             mp.COA_SCHEMA)
    world, _characters = mp.build_sql(server_report)
    (mp.STAGE / "world.sql").write_bytes(world)
    server_report["client_display_moves"] = {kind: len(pairs) for kind, pairs in moved.items()}
    report_path.write_text(json.dumps(server_report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    report = {"rows": counts, "server_rows": server_rows, "stats": dict(work.stats), "asset_files": len(assets),
              "missing_files_by_folder": dict(work.missing_files.most_common(40)),
              "display_moves": {kind: len(pairs) for kind, pairs in moved.items()}}
    (OUT / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))


# -------------------------------------------------------------------------------------------------------- pack
def pack(_args):
    lines = [line.split("\t") for line in (OUT / "assets.tsv").read_text(encoding="latin-1").splitlines() if line]
    names = [name for _, name in lines if name.isascii()]
    if len(names) != len(lines):
        print(f"skipped {len(lines) - len(names)} non-ASCII names")
    coa = ed.Archives(COA_CLIENT)
    sources = sorted({coa.paths[[p.name for p in coa.paths].index(archive)] for archive, _ in lines},
                     key=lambda p: p.name.lower(), reverse=True)  # the winning archive is tried first
    coa.close()
    ei.pack_assets(ed.storm(), sources, names, OUT / ARCHIVE)
    print(f"{len(names)} files -> {OUT / ARCHIVE}")


# ----------------------------------------------------------------------------------------------------- install
def install(args):
    """Append the deltas to the client's live registry (new revision; old one kept) and drop in the MPQ."""
    root = args.client / "Data/Retail"
    backup = args.client / "_backups" / f"manastorm-{time.strftime('%Y%m%d-%H%M%S')}"
    backup.mkdir(parents=True)
    shutil.copy2(root / "RetailCasc.ini", backup / "RetailCasc.ini")
    config = dict(line.strip().split("=", 1) for line in (root / "RetailCasc.ini").read_text().splitlines() if "=" in line)
    current = root / config["tables"]
    revision = "manastorm-" + n.digest(b"".join(p.read_bytes() for p in sorted((OUT / "tables").glob("*.dbc"))))[:12]
    registry = root / "Registries" / revision
    if registry.exists():
        raise SystemExit(f"{registry} already exists")
    staging = registry.with_name(revision + ".tmp")
    shutil.rmtree(staging, ignore_errors=True)
    shutil.copytree(current, staging)
    for delta in sorted((OUT / "tables").glob("*.dbc")):
        name = next(t for t in LAYOUT if t.lower() == delta.stem)
        fields, string_cols = LAYOUT[name]
        rows, values = [], {}
        target = staging / delta.name
        for path in ([target] if target.is_file() else []) + [delta]:
            table_rows, pool = n.wdbc(path.read_bytes(), fields)
            for row in table_rows:
                for c in string_cols:
                    if row[c]:
                        values[len(rows), c] = ed.cstr(pool, row[c])
                rows.append(row)
        if len({row[0] for row in rows}) != len(rows):
            raise SystemExit(f"{name}: an ID is already in the registry")
        target.write_bytes(n.make_wdbc(fields, rows, values))
    staging.rename(registry)
    lines = []
    for line in (root / "RetailCasc.ini").read_text().splitlines():
        if line.startswith("tables="):
            line = f"tables=Registries/{revision}"
        elif line.strip() == f"aliases={config['tables']}/aliases.csv":
            line = f"aliases=Registries/{revision}/aliases.csv"
        lines.append(line)
    (root / "RetailCasc.ini.tmp").write_text("\n".join(lines) + "\n", encoding="ascii")
    (root / "RetailCasc.ini.tmp").replace(root / "RetailCasc.ini")
    archive = args.client / "Data" / ARCHIVE
    if archive.exists() and n.digest(archive.read_bytes()) != n.digest((OUT / ARCHIVE).read_bytes()):
        raise SystemExit(f"{archive} exists with different content")
    shutil.copy2(OUT / ARCHIVE, archive)
    print(json.dumps({"registry": revision, "previous": config["tables"], "archive": str(archive), "backup": str(backup)}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("stage", "pack", "install"))
    parser.add_argument("--client", type=Path, default=ed.CLIENT)
    args = parser.parse_args()
    {"stage": stage, "pack": pack, "install": install}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
