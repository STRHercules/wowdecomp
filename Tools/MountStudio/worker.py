"""Native Retail mount inventory/acquisition/MPQ worker. No asset conversion or installation."""
from __future__ import annotations

import contextlib
import csv
import ctypes as c
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
import urllib.request

TABLE_IDS = {"Mount": 921760, "MountXDisplay": 1576117,
             "CreatureDisplayInfo": 1108759, "CreatureModelData": 1365368}
ADVFLY = {1520, 1522, 1524, 1528, 1530, 1532, 1534, 1556, 1558, 1588,
          1618, 1620, 1622, 1628, 1630, 1634, 1638, 1678, 1680, 1702, 1704, 1706, 1722, 1724, 1726}
MAX_ASSET = 128 * 1024 * 1024
MAX_ARCHIVE = 1800 * 1024 * 1024
STOCK_PATCH = re.compile(r"patch(?:-[a-z]{4})?(?:-[23])?\.mpq$", re.I)
BUNDLED = Path(__file__).resolve().parent / "runtime"


def emit(kind, **data):
    print(json.dumps({"type": kind, **data}, ensure_ascii=True), flush=True)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_hash(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def save_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    os.replace(temp, path)


def read_ini(path):
    values = {}
    for line in Path(path).read_text(encoding="utf-8-sig").splitlines():
        line = line.strip()
        if not line or line[0] in "#;":
            continue
        key, separator, value = line.partition("=")
        key = key.strip().lower()
        if not separator or key in values:
            raise ValueError("Invalid or duplicate source config key")
        values[key] = value.strip()
    if values.get("product", "wow") != "wow":
        raise ValueError("Select a Retail wow source, not Forever or a test product")
    if not re.fullmatch(r"[0-9a-fA-F]{32}", values.get("build", "")):
        raise ValueError("Source must pin a 32-digit build configuration key")
    return values


def local_source_info(path):
    if not path or not Path(path).is_absolute():
        raise ValueError("Choose your installed Retail folder in Settings, or select the CDN source")
    folder = Path(path).resolve()
    for root in (folder, folder.parent):
        build_info = root / ".build.info"
        if not build_info.is_file() or not (root / "Data").is_dir():
            continue
        with build_info.open(encoding="utf-8-sig", newline="") as stream:
            rows = csv.reader(stream, delimiter="|")
            columns = next(rows, None)
            if not columns:
                raise ValueError("Retail build information is empty; wait for the installation to finish")
            header = [column.split("!", 1)[0].strip() for column in columns]
            products = [dict(zip(header, (value.strip() for value in values))) for values in rows if values]
        retail = [row for row in products if row.get("Product") == "wow" and row.get("Active") == "1"]
        builds = {row.get("Build Key", "").lower() for row in retail}
        if len(builds) != 1 or not re.fullmatch(r"[a-f0-9]{32}", next(iter(builds), "")):
            raise ValueError("The folder has no single active Retail wow build; Classic/Forever is a different product")
        return {"root": root, "build": next(iter(builds)), "version": retail[0].get("Version", "")}
    raise ValueError("Retail folder must contain .build.info and Data, or be its _retail_ subfolder")


def bounded(value, default, minimum, maximum, name):
    try:
        if isinstance(value, bool) or (isinstance(value, float) and not value.is_integer()):
            raise ValueError()
        number = int(value if value not in (None, "") else default)
    except (TypeError, ValueError):
        raise ValueError(f"{name} must be a whole number") from None
    if not minimum <= number <= maximum:
        raise ValueError(f"{name} must be between {minimum} and {maximum}")
    return number


def wdbc(data, fields):
    if len(data) < 20:
        raise ValueError("Truncated WDBC header")
    magic, count, actual, stride, strings = struct.unpack_from("<4s4I", data)
    end = 20 + count * stride
    if magic != b"WDBC" or actual != fields or stride != fields * 4 or len(data) != end + strings:
        raise ValueError(f"Invalid WDBC layout: expected {fields} columns")
    rows = {}
    for index in range(count):
        row = list(struct.unpack_from(f"<{fields}I", data, 20 + index * stride))
        if row[0] in rows:
            raise ValueError(f"Duplicate WDBC record {row[0]}")
        rows[row[0]] = row
    return rows, data[end:]


def text(pool, offset):
    if offset >= len(pool) or pool.find(b"\0", offset) < 0:
        raise ValueError("Invalid DBC string offset")
    return pool[offset:pool.index(0, offset)].decode("utf-8", errors="replace")


class Storm:
    """Serialize all archive access; always compress and read back published entries."""
    def __init__(self, dll):
        self.dll = c.WinDLL(str(Path(dll).resolve()), use_last_error=True)
        handle, word = c.c_void_p, c.c_uint32
        signatures = {
            "SFileOpenArchive": ([c.c_wchar_p, word, word, c.POINTER(handle)], c.c_bool),
            "SFileCloseArchive": ([handle], c.c_bool),
            "SFileOpenFileEx": ([handle, c.c_char_p, word, c.POINTER(handle)], c.c_bool),
            "SFileGetFileSize": ([handle, c.POINTER(word)], word),
            "SFileReadFile": ([handle, c.c_void_p, word, c.POINTER(word), handle], c.c_bool),
            "SFileCloseFile": ([handle], c.c_bool),
            "SFileCreateArchive": ([c.c_wchar_p, word, word, c.POINTER(handle)], c.c_bool),
            "SFileAddFileEx": ([handle, c.c_wchar_p, c.c_char_p, word, word, word], c.c_bool),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.dll, name)
            fn.argtypes, fn.restype = args, result

    @contextlib.contextmanager
    def archive(self, path):
        handle = c.c_void_p()
        if not self.dll.SFileOpenArchive(str(path), 0, 0x100, c.byref(handle)):
            raise OSError(f"Cannot open archive {path}: {c.get_last_error()}")
        try:
            yield handle
        finally:
            self.dll.SFileCloseArchive(handle)

    def read(self, archive, name, missing=False, limit=512 * 1024 * 1024):
        handle = c.c_void_p()
        if not self.dll.SFileOpenFileEx(archive, name.encode("ascii"), 0, c.byref(handle)):
            error = c.get_last_error()
            if missing and error in (2, 3):
                return None
            raise OSError(f"Cannot read {name}: StormLib error {error}")
        try:
            high = c.c_uint32()
            size = self.dll.SFileGetFileSize(handle, c.byref(high))
            if high.value or size > limit:
                raise ValueError(f"Archive entry too large: {name}")
            buffer, got = c.create_string_buffer(size or 1), c.c_uint32()
            if not self.dll.SFileReadFile(handle, buffer, size, c.byref(got), None) or got.value != size:
                raise OSError(f"Short archive read: {name}")
            return buffer.raw[:size]
        finally:
            self.dll.SFileCloseFile(handle)

    def pack(self, path, entries):
        handle = c.c_void_p()
        partial = path.with_suffix(".partial")
        if partial.exists():
            partial.unlink()
        if not self.dll.SFileCreateArchive(str(partial), 1, len(entries) + 16, c.byref(handle)):
            raise OSError(f"Cannot create MPQ: {c.get_last_error()}")
        try:
            for name, source, expected in entries:
                if file_hash(source) != expected:
                    raise ValueError(f"Source changed during export: {source}")
                # MPQ_FILE_COMPRESS, MPQ_COMPRESSION_ZLIB. Never store uncompressed originals by accident.
                if not self.dll.SFileAddFileEx(handle, str(source), name.encode("ascii"), 0x200, 2, 2):
                    raise OSError(f"Cannot pack {name}: {c.get_last_error()}")
        finally:
            self.dll.SFileCloseArchive(handle)
        if partial.stat().st_size >= 2 * 1024 ** 3 - 64 * 1024 ** 2:
            raise ValueError("Archive exceeds the client's verified classic MPQ size boundary")
        with self.archive(partial) as archive:
            for name, _, expected in entries:
                if digest(self.read(archive, name, limit=MAX_ASSET)) != expected:
                    raise ValueError(f"MPQ readback hash mismatch: {name}")
        os.replace(partial, path)


def archive_order(client, locale):
    """Mirror PatchFiles' lexical priority, including every custom patch suffix."""
    data = client / "Data"
    bases = ["common.MPQ", "common-2.MPQ", "common-3.MPQ", "expansion.MPQ", "lichking.MPQ"]
    local_bases = [f"locale-{locale}.MPQ", f"expansion-locale-{locale}.MPQ", f"lichking-locale-{locale}.MPQ"]
    found = {p.name.lower(): p for p in data.iterdir() if p.is_file()}
    local = data / locale
    local_found = {p.name.lower(): p for p in local.iterdir() if p.is_file()} if local.is_dir() else {}
    result = [found[n.lower()] for n in bases if n.lower() in found]
    result += [local_found[n.lower()] for n in local_bases if n.lower() in local_found]
    patches = [p for p in [*found.values(), *local_found.values()]
               if p.suffix.lower() == ".mpq" and p.name.lower().startswith("patch")]
    # PatchFiles.cpp sorts full relative paths descending, then OpenArchives opens them backwards.
    # Its pass-1 patch.MPQ providers open before the sorted patch-* group and have lower priority.
    plain = [p for p in patches if p.name.lower() in ("patch.mpq", f"patch-{locale.lower()}.mpq")]
    named = [p for p in patches if p not in plain]
    return result + plain + sorted(named, key=lambda p: p.relative_to(client).as_posix().lower())


def mount_type(spell):
    auras = set(spell[95:98])
    if auras & {201, 206, 207, 208, 209, 210, 211}:
        return "flying"
    if 58 in auras:
        return "water"
    return "ground"


def creature_models(creature_ids, settings=None):
    # Reuse the existing credential-free, read-only Docker database helper in source and release layouts.
    mode = (settings or {}).get("mapping_mode", "live")
    if mode in ("bundled", "file"):
        path = Path((settings or {}).get("mapping_file", BUNDLED / "mount-models.json"))
        payload = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("Mount model mapping must be a JSON object of creature IDs to display IDs")
        result = {}
        for creature in creature_ids:
            values = payload.get(str(creature), [])
            if not isinstance(values, list) or any(type(value) is not int or not 0 < value <= 0xFFFFFFFF
                                                   for value in values):
                raise ValueError(f"Invalid mount display mapping for creature {creature}")
            if values:
                result[str(creature)] = sorted(set(values))
        return result
    if mode != "live":
        raise ValueError("Choose bundled, file, or live mount mappings")
    local = Path(__file__).resolve().parent
    helper = local if (local / "server.py").is_file() else local.parent / "RetailAssets"
    sys.path.insert(0, str(helper))
    from server import query
    ids = sorted({int(value) for value in creature_ids if 0 < int(value) <= 0xFFFFFFFF})
    if not ids:
        return {}
    sql = ("SELECT CreatureID, CreatureDisplayID FROM creature_template_model WHERE Probability > 0 "
           f"AND CreatureID IN ({','.join(map(str, ids))}) ORDER BY CreatureID, Idx;")
    result = {}
    options = {"container": settings.get("database_container", "ac-database"), "timeout": 60} if settings else {}
    for line in query(sql, **options).splitlines():
        creature, display = map(int, line.split("\t"))
        if display > 0:
            result.setdefault(creature, []).append(display)
    return {str(key): sorted(set(values)) for key, values in result.items()}


def resolve_mounts(spells, spell_strings, displays, display_strings, models, model_strings, stock_ids, mapping):
    mounts = {}
    for spell in spells.values():
        creatures = sorted({spell[110 + i] for i in range(3) if spell[95 + i] == 78 and spell[110 + i]})
        for creature in creatures:
            for display_id in mapping.get(str(creature), []):
                display = displays.get(display_id)
                model = models.get(display[1]) if display else None
                name = text(spell_strings, spell[136])
                model_path = text(model_strings, model[2]) if model else ""
                protected = spell[0] not in stock_ids or model_path.lower().startswith(("custom", "retail", "forever"))
                row = mounts.setdefault(display_id, {
                    "key": f"display:{display_id}", "spell": spell[0], "spells": [], "creatures": [],
                    "mount_types": [], "name": name, "display": display_id, "model_path": model_path,
                    "textures": [text(display_strings, display[i]) for i in (6, 7, 8)] if display else [],
                    "protected": False, "selected": False, "source": None, "candidates": [],
                    "status": "unmatched", "error": ""})
                if spell[0] not in {variant["id"] for variant in row["spells"]}:
                    row["spells"].append({"id": spell[0], "name": name, "type": mount_type(spell)})
                row["creatures"] = sorted(set(row["creatures"]) | {creature})
                row["mount_types"] = sorted({variant["type"] for variant in row["spells"]})
                row["protected"] |= protected
                if row["protected"]:
                    row["status"] = "protected"
    for row in mounts.values():
        row["spells"].sort(key=lambda variant: ("[PH]" in variant["name"], variant["id"]))
        row["spell"], row["name"] = row["spells"][0]["id"], row["spells"][0]["name"]
    return sorted(mounts.values(), key=lambda row: (row["protected"], row["name"].casefold(), row["spell"]))


def match_mounts(mounts, catalog):
    for row in mounts:
        if row["protected"]:
            continue
        spell_ids = {variant["id"] for variant in row.get("spells", [{"id": row["spell"]}])}
        candidates = list({(source["mount"], source["display"]): source
                           for source in catalog if source["spell"] in spell_ids}.values())
        exact = [source for source in candidates if source["display"] == row["display"]]
        if exact:
            candidates = exact
        row["candidates"] = candidates
        if len(candidates) == 1:
            row["source"], row["status"] = candidates[0], "matched"
            row["selected"] = "flying" in row.get("mount_types", [])
        else:
            row["source"], row["selected"] = None, False
            row["status"] = "ambiguous" if candidates else "unmatched"


def restore_choices(mounts, previous, legacy=False):
    for row in mounts:
        variants = {variant["id"] for variant in row["spells"]}
        old = [value for value in previous if value.get("source") and not value.get("protected")
               and (value["display"] in row["creatures"] if legacy else value["display"] == row["display"])
               and (value.get("spell") in variants)]
        if row["protected"] or not old:
            continue
        sources = {json.dumps(value["source"], sort_keys=True) for value in old}
        if len(sources) != 1:
            row["source"], row["selected"], row["status"] = None, False, "ambiguous"
            row["error"] = "Saved spell variants chose different appearances; review this group"
            continue
        chosen = next((value for value in old if value["status"] == "validated"), old[0])
        for key in ("source", "status", "error", "files", "advfly"):
            if key in chosen:
                row[key] = chosen[key]
        row["selected"] = all(value["selected"] for value in old)


def chunks(data):
    position, seen = 0, set()
    while position < len(data):
        if len(data) - position < 8:
            raise ValueError("Truncated retail chunk header")
        tag, size = struct.unpack_from("<4sI", data, position)
        position += 8
        if size > len(data) - position or tag in seen:
            raise ValueError(f"Invalid or duplicate retail chunk {tag!r}")
        seen.add(tag)
        yield tag, data[position:position + size]
        position += size


def sequences(data, offset):
    if len(data) < offset + 8:
        raise ValueError("Truncated sequence descriptor")
    count, start = struct.unpack_from("<II", data, offset)
    if count * 64 + start > len(data):
        raise ValueError("Sequence table outside file")
    return {struct.unpack_from("<H", data, start + index * 64)[0] for index in range(count)}


def dependencies(data, extension):
    """Retain all AFID variations, every advertised skin and the complete parent-SKEL chain."""
    if extension not in ("m2", "skel"):
        return [], set()
    parts = [(b"MD21", data)] if data[:4] == b"MD20" else list(chunks(data))
    result, actions = set(), set()
    for tag, payload in parts:
        if tag in (b"SFID", b"TXID", b"SKID", b"BFID", b"PFID", b"GPID", b"RPID"):
            if len(payload) % 4 or (tag == b"SKID" and len(payload) != 4):
                raise ValueError(f"Invalid {tag.decode()} array")
            suffix = {b"SFID": "skin", b"TXID": "blp", b"SKID": "skel", b"BFID": "bone",
                      b"PFID": "phys", b"GPID": "m2", b"RPID": "m2"}[tag]
            result.update((value, suffix) for (value,) in struct.iter_unpack("<I", payload) if value)
        elif tag == b"AFID":
            if len(payload) % 8:
                raise ValueError("Invalid external animation table")
            for action, _, file_id in struct.iter_unpack("<HHI", payload):
                actions.add(action)
                if file_id:
                    result.add((file_id, "anim"))
        elif tag == b"SKPD":
            if len(payload) < 12:
                raise ValueError("Invalid parent skeleton")
            parent = struct.unpack_from("<I", payload, 8)[0]
            if parent:
                result.add((parent, "skel"))
        elif tag in (b"MD21", b"SKS1"):
            actions.update(sequences(payload, 0x1C if tag == b"MD21" else 8))
    return sorted(result), actions


class Paused(Exception):
    pass


class Studio:
    def __init__(self, request):
        self.settings = request["settings"]
        if not Path(self.settings["client"]).is_absolute():
            raise ValueError("Choose the target 3.3.5 client folder in Settings")
        self.client = Path(self.settings["client"]).resolve()
        self.work = Path(self.settings["work"]).resolve()
        if not Path(self.settings["work"]).is_absolute() or self.work == self.work.parent:
            raise ValueError("Choose an absolute dedicated work directory")
        if self.work == self.client or self.client in self.work.parents:
            raise ValueError("Work/output directory must be outside the client")
        if self.work in self.client.parents:
            raise ValueError("Work directory must not contain the client")
        self.work.mkdir(parents=True, exist_ok=True)
        self.project_path = self.work / "project.json"
        self.project = json.loads(self.project_path.read_text()) if self.project_path.exists() else {
            "schema": 2, "mounts": [], "catalog": [], "assets": {}, "source": None}
        if self.project["schema"] not in (1, 2):
            raise ValueError("Unsupported project schema")
        if self.project.get("client") and self.project["client"] != str(self.client):
            raise ValueError("This work directory belongs to a different client")
        self.project["client"] = str(self.client)
        self.project["settings"] = self.settings
        self.pause = self.work / "pause.request"
        output = self.settings.get("export_dir", "")
        self.export_root = Path(output).resolve() if output else self.work / "exports"
        if output and not Path(output).is_absolute():
            raise ValueError("MPQ output directory must be absolute")
        if self.export_root == self.client or self.client in self.export_root.parents:
            raise ValueError("MPQ output must be outside the target client; packages are staged before installation")
        retail = self.settings.get("retail_root", "")
        if retail:
            retail_path = Path(retail).resolve()
            if retail_path.name.casefold() == "_retail_":
                retail_path = retail_path.parent
            if any(path == retail_path or retail_path in path.parents for path in (self.work, self.export_root)):
                raise ValueError("Work and MPQ output folders must be outside the Retail installation")

    def checkpoint(self):
        if self.pause.exists():
            raise Paused()

    def persist(self):
        save_json(self.project_path, self.project)

    def result(self):
        self.persist()
        emit("project", project=self.project)

    def load(self):
        if self.project["schema"] == 1:
            self.scan(preserve=True)
        else:
            self.result()

    def scan(self, preserve=False):
        storm = Storm(self.settings["storm"])
        raw_tables, stock, providers = {}, {}, []
        names = {"Spell": 234, "CreatureDisplayInfo": 16, "CreatureModelData": 28}
        for archive_path in archive_order(self.client, self.settings["locale"]):
            self.checkpoint()
            emit("progress", message=f"Inspecting {archive_path.name}")
            with storm.archive(archive_path) as archive:
                for name, fields in names.items():
                    data = storm.read(archive, f"DBFilesClient\\{name}.dbc", missing=True)
                    if data is None:
                        continue
                    raw_tables[name] = data
                    providers.append({"table": name, "path": str(archive_path), "sha256": digest(data)})
                    if (name == "Spell" and STOCK_PATCH.fullmatch(archive_path.name)
                            and struct.unpack_from("<I", data, 8)[0] == 234):
                        stock.update(wdbc(data, fields)[0])
        # Older shadowed archives use older schemas. Only the actual winning table needs the target layout.
        tables = {name: wdbc(data, names[name]) for name, data in raw_tables.items()}
        # The active native source merges these supplemental rows after MPQ/loose tables.
        configured = self.settings.get("client_config", "")
        source_path = Path(configured) if configured else self.client / "Data/Retail/RetailCasc.ini"
        config = read_ini(source_path) if source_path.is_file() else {}
        if config.get("tables"):
            for name, fields in names.items():
                path = source_path.parent / config["tables"] / f"{name.lower()}.dbc"
                if path.is_file():
                    data = path.read_bytes()
                    extra, pool = wdbc(data, fields)
                    base, base_pool = tables.get(name, ({}, b"\0"))
                    string_fields = {"Spell": range(136, 204), "CreatureDisplayInfo": (6, 7, 8, 9),
                                     "CreatureModelData": (2,)}[name]
                    for row in extra.values():
                        # Native MergeClientTable appends only; collisions are an invalid baseline.
                        if row[0] in base:
                            raise ValueError(f"Supplemental {name} collides with existing ID {row[0]}")
                        row = row.copy()
                        for field in string_fields:
                            if name == "Spell" and field % 17 == 16:
                                continue
                            if row[field]:
                                text(pool, row[field])
                                row[field] += len(base_pool)
                        base[row[0]] = row
                    tables[name] = base, base_pool + pool
                    providers.append({"table": name, "path": str(path), "sha256": digest(data)})
        if not stock or set(tables) != set(names):
            raise ValueError("Cannot establish stock mount IDs and all effective client tables")
        creature_ids = {spell[110 + index] for spell in tables["Spell"][0].values() for index in range(3)
                        if spell[95 + index] == 78 and spell[110 + index]}
        emit("progress", message="Resolving mount creatures through the world database (read-only)")
        mapping = creature_models(creature_ids, self.settings)
        if not mapping:
            raise ValueError("No mount display mappings found; check the mapping source in Settings")
        previous, legacy = self.project["mounts"], self.project["schema"] == 1
        mounts = resolve_mounts(*tables["Spell"], *tables["CreatureDisplayInfo"],
                                *tables["CreatureModelData"], set(stock), mapping)
        if self.project["catalog"]:
            match_mounts(mounts, self.project["catalog"])
        if preserve:
            restore_choices(mounts, previous, legacy)
        if legacy:
            backup = self.work / ("project-v1-backup-" + time.strftime("%Y%m%d-%H%M%S") + ".json")
            save_json(backup, json.loads(self.project_path.read_text()))
        self.project["schema"], self.project["mounts"] = 2, mounts
        self.project["creature_ids"], self.project["creature_models"] = sorted(creature_ids), mapping
        unresolved = sorted(creature_ids - {int(key) for key in mapping})
        self.project["unresolved_creatures"] = unresolved
        if unresolved:
            emit("progress", message=f"{len(unresolved)} mount creature entries "
                 "have no positive-probability server model")
        self.project["providers"] = providers
        self.project["archive_order"] = [str(p) for p in archive_order(self.client, self.settings["locale"])]
        self.result()

    def source(self):
        mode = self.settings.get("source_mode", "local")
        locale = self.settings.get("source_locale", "2")
        if not re.fullmatch(r"[0-9a-fA-F]{1,8}", str(locale)) or not int(locale, 16):
            raise ValueError("Retail locale must be a nonzero hexadecimal mask")
        if mode == "local":
            installed = local_source_info(self.settings.get("retail_root", ""))
            values = {"mode": "local", "path": installed["root"].as_posix(), "build": installed["build"]}
            info = {"mode": mode, "version": installed["version"], "path": values["path"]}
        elif mode == "cdn":
            original = Path(self.settings.get("source_config", BUNDLED / "config/RetailCasc.ini"))
            values = read_ini(original)
            values["build"] = self.settings.get("cdn_build") or values["build"]
            values["region"] = self.settings.get("cdn_region") or values.get("region", "us")
            if not re.fullmatch(r"[0-9a-fA-F]{32}", values["build"]):
                raise ValueError("CDN build configuration key must contain 32 hexadecimal digits")
            values["mode"], values["path"] = "online", (self.work / "cdn").as_posix()
            info = {"mode": mode, "region": values["region"]}
        else:
            raise ValueError("Choose Local Retail or CDN in Settings")
        source = {"product": "wow", "build": values["build"].lower(), "locale": str(locale).lower()}
        if self.project["source"] and self.project["source"] != source:
            raise ValueError("This project uses another Retail build or locale. Choose a new project work folder, "
                             "or use the same build as the existing project")
        self.project["source"] = source
        self.project["source_info"] = info
        lines = [f"mode={values['mode']}", f"path={values['path']}", "product=wow",
                 f"build={source['build']}", f"locale={source['locale']}"]
        if mode == "cdn":
            lines.append(f"region={values['region']}")
            for key in ("cdn", "version"):
                if values.get(key):
                    lines.append(f"{key}={values[key]}")
        if self.settings.get("keys_file"):
            key_file = Path(self.settings["keys_file"]).resolve()
            if not key_file.is_file():
                raise ValueError("The selected encryption key file does not exist")
            lines.append(f"keys={key_file.as_posix()}")
        config = self.work / "acquire.ini"
        config.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return config

    def probe(self, config, name, destination=None):
        self.checkpoint()
        args = [self.settings["probe"], str(config), name]
        if destination is not None:
            args += ["--extract-table" if name.endswith(".db2") else "--extract", str(destination)]
        kwargs = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
        timeout = bounded(self.settings.get("download_timeout"), 600, 30, 3600, "Per-file timeout")
        environment = dict(os.environ, WOW_RETAIL_BUSY_NOTICE="0")
        completed = subprocess.run(args, capture_output=True, text=True, timeout=timeout, env=environment, **kwargs)
        if completed.returncode:
            raise RuntimeError((completed.stderr or completed.stdout or "RetailAssetProbe failed")[-6000:])
        self.checkpoint()

    def fetch(self, config, file_id, extension):
        self.checkpoint()
        if not 0 < int(file_id) <= 0xFFFFFFFF:
            raise ValueError("FileDataID is outside the unsigned 32-bit range")
        key = str(int(file_id))
        originals = self.work / "originals"
        originals.mkdir(exist_ok=True)
        path = originals / f"{key}.bin"
        saved = self.project["assets"].get(key)
        if saved and path.is_file() and file_hash(path) == saved["sha256"]:
            emit("progress", message=f"Reusing project cache: retail/{key}.{extension}")
            return path
        temp = path.with_suffix(".download")
        temp.unlink(missing_ok=True)
        mode = self.project.get("source_info", {}).get("mode", self.settings.get("source_mode", "local"))
        origin = "Reading local Retail data" if mode == "local" else "Accessing CDN/cache"
        emit("progress", message=f"{origin}: retail/{key}.{extension}")
        self.probe(config, f"retail/{key}.{extension}", temp)
        size = temp.stat().st_size
        if not size or size > MAX_ASSET:
            raise ValueError(f"Invalid asset size for {key}")
        os.replace(temp, path)
        self.project["assets"][key] = {"sha256": file_hash(path), "size": size, "extension": extension}
        self.persist()
        return path

    def catalog(self):
        kwargs = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
        capabilities = subprocess.run([self.settings["probe"], "--capabilities"],
                                      capture_output=True, text=True, timeout=30, **kwargs)
        try:
            supports_tables = (not capabilities.returncode
                               and json.loads(capabilities.stdout).get("table_extract") is True)
        except (ValueError, AttributeError):
            supports_tables = False
        if not supports_tables:
            raise ValueError("Rebuild RetailAssetProbe from this checkout: "
                             "MountStudio requires --extract-table support")
        config = self.source()
        reader = Path(self.settings["reader"])
        if not (reader / "wotlkconv/db/db2.py").is_file():
            raise ValueError("Select the existing DB2 reader source directory (no converter is executed)")
        sys.path.insert(0, str(reader))
        from wotlkconv.db.db2 import parse_db2
        from wotlkconv.db.dbd import DbdIndex
        definitions = (Path(self.settings["definitions"]) if self.settings.get("definitions")
                       else self.work / "definitions")
        definitions.mkdir(parents=True, exist_ok=True)
        revision_file = definitions / "source-revision.json"
        if not self.settings.get("definitions") and not revision_file.exists():
            request = urllib.request.Request("https://api.github.com/repos/wowdev/WoWDBDefs/commits/master",
                                             headers={"User-Agent": "EsteriaMountStudio"})
            with urllib.request.urlopen(request, timeout=60) as response:
                revision = json.load(response)["sha"]
            if not re.fullmatch(r"[0-9a-f]{40}", revision):
                raise ValueError("Invalid WoWDBDefs revision")
            save_json(revision_file, {"revision": revision})
        tables, provenance = {}, {}
        for name, file_id in TABLE_IDS.items():
            self.checkpoint()
            definition = definitions / f"{name}.dbd"
            if not definition.exists() and not self.settings.get("definitions"):
                revision = json.loads(revision_file.read_text())["revision"]
                url = f"https://raw.githubusercontent.com/wowdev/WoWDBDefs/{revision}/definitions/{name}.dbd"
                with urllib.request.urlopen(url, timeout=60) as response:
                    definition.write_bytes(response.read(2 * 1024 * 1024))
            if not definition.exists():
                raise ValueError(f"Missing schema {definition}")
            original = self.fetch(config, file_id, "db2")
            table = parse_db2(original.read_bytes(), name + ".db2", DbdIndex(definitions))
            if not table.named:
                raise ValueError(f"{name}: no matching named DB2 schema")
            if table.skipped_records or table.encrypted_sections:
                emit("progress", message=f"{name}: {table.skipped_records} unavailable records in "
                     f"{table.encrypted_sections} encrypted sections; unresolved mounts will stay unselected")
            tables[name] = table.rows
            provenance[name] = {"file_id": file_id, "sha256": file_hash(original),
                                "schema_sha256": file_hash(definition), "layout": f"{table.layout_hash:08x}",
                                "encrypted_sections": table.encrypted_sections,
                                "skipped_records": table.skipped_records}
        displays, models = tables["CreatureDisplayInfo"], tables["CreatureModelData"]
        catalog = []
        for link in tables["MountXDisplay"].values():
            mount = tables["Mount"].get(link["MountID"])
            display_id = link["CreatureDisplayInfoID"]
            display = displays.get(display_id)
            model = models.get(display["ModelID"]) if display else None
            if not mount or not model or not model.get("FileDataID"):
                continue
            textures = display.get("TextureVariationFileDataID", [])
            if not isinstance(textures, list):
                textures = [textures]
            # A fourth variation texture is dropped: no retail mount M2 has a texture of a type past 13 to receive it.
            unsupported = ""
            catalog.append({"mount": mount["ID"], "spell": mount["SourceSpellID"],
                            "display": display_id, "name": mount.get("Name_lang", mount.get("Name", "")),
                            "model": model["FileDataID"], "textures": textures[:3],
                            "scale": float(display.get("CreatureModelScale", 1)),
                            "unsupported": unsupported})
        if not catalog:
            raise ValueError("No retail mounts resolved; check metadata schemas")
        self.project["catalog"], self.project["metadata"] = catalog, provenance
        match_mounts(self.project["mounts"], catalog)
        self.result()

    def selections(self, request):
        if self.project["schema"] == 1:
            raise ValueError("Open saved project to upgrade its mount inventory before saving or exporting")
        edits = {row["key"]: row for row in request.get("edits", [])}
        catalog = {(row["mount"], row["display"]): row for row in self.project["catalog"]}
        for row in self.project["mounts"]:
            edit = edits.get(row["key"])
            if not edit or row["protected"]:
                continue
            source_key = (edit.get("mount"), edit.get("display"))
            source = catalog.get(source_key)
            if source != row["source"]:
                row["files"], row["advfly"], row["error"] = [], [], ""
                row["status"] = "matched" if source else "unmatched"
            row["source"] = source
            row["selected"] = bool(edit.get("selected") and source)
        self.persist()

    def download(self):
        config = self.source()
        for row in self.project["mounts"]:
            if not row["selected"] or row["protected"] or not row["source"]:
                continue
            self.checkpoint()
            row["status"], row["error"] = "acquiring", ""
            self.persist()
            emit("mount", mount=row)
            try:
                if row["source"].get("unsupported"):
                    raise ValueError(row["source"]["unsupported"])
                pending = [(row["source"]["model"], "m2")]
                pending += [(value, "blp") for value in row["source"]["textures"] if value]
                seen, actions = set(), set()
                while pending:
                    file_id, extension = pending.pop()
                    if (file_id, extension) in seen:
                        continue
                    if len(seen) >= 10000:
                        raise ValueError("Asset dependency graph exceeds 10,000 files")
                    seen.add((file_id, extension))
                    path = self.fetch(config, file_id, extension)
                    refs, animation_ids = dependencies(path.read_bytes(), extension)
                    pending.extend(refs)
                    actions.update(animation_ids)
                offline = self.work / "validate.ini"
                pin = self.project["source"]
                offline.write_text(f"mode=files\npath=originals\nproduct=wow\nbuild={pin['build']}\n"
                                   f"locale={pin['locale']}\n", encoding="utf-8")
                # Run the engine's actual native preparation against originals with network unavailable.
                self.probe(offline, f"retail/{row['source']['model']}.m2")
                row["files"] = sorted({str(file_id) for file_id, _ in seen}, key=int)
                row["advfly"] = sorted(actions & ADVFLY)
                row["status"] = "validated"
            except Paused:
                row["status"] = "paused"
                self.persist()
                raise
            except Exception as error:
                row["status"], row["error"] = "failed", str(error)
            self.persist()
            emit("mount", mount=row)
        self.result()

    def export(self, ready_only=False):
        selected = [row for row in self.project["mounts"] if row["selected"] and not row["protected"]]
        ready = [row for row in selected if row["status"] == "validated"]
        skipped = [row for row in selected if row["status"] != "validated"]
        if not ready:
            raise ValueError("No selected mounts are ready to export. Acquire and validate at least one mount first.")
        if skipped and not ready_only:
            raise ValueError(f"{len(ready)} of {len(selected)} selected mounts are ready. "
                             "Choose Export completed mounts to package them, or resume acquisition for the rest.")
        chosen = ready
        self.source()  # Reject changes to the pinned build even when all assets were cached.
        overrides, files = {}, set()
        protected_displays = {row["display"] for row in self.project["mounts"] if row["protected"]}
        excluded_displays = {row["display"] for row in self.project["mounts"] if not row["selected"]}
        for row in chosen:
            if row["display"] in protected_displays:
                raise ValueError(f"Display {row['display']} is shared with a protected custom mount; exclude it")
            if row["display"] in excluded_displays:
                raise ValueError(f"Display {row['display']} is shared with an excluded spell; "
                                 "review all its rows together")
            source = row["source"]
            old = overrides.setdefault(row["display"], source)
            if old != source:
                raise ValueError(f"Conflicting replacements share target display {row['display']}")
            files.update(row["files"])
        self.verify_baseline()
        source = self.project["source"]
        scope = f"{source['product']}-{source['build']}\\{int(source['locale'], 16)}"
        export_id = time.strftime("%Y%m%d-%H%M%S") + "-" + os.urandom(3).hex()
        output = self.export_root / export_id
        output.mkdir(parents=True, exist_ok=False)
        mapping = output / "mounts.csv"
        lines = ["# target-display;product;build;locale;model;texture0;texture1;texture2;scale"]
        for display, model in sorted(overrides.items()):
            textures = (model["textures"] + [0, 0, 0])[:3]
            scale = model["scale"]
            if not math.isfinite(scale) or not 0 < scale <= 100:
                raise ValueError("Invalid source mount scale")
            lines.append(";".join(map(str, [display, source["product"], source["build"], source["locale"],
                                           model["model"], *textures, scale])))
        mapping.write_text("\n".join(lines) + "\n", encoding="ascii")
        entries = [("EsteriaMounts\\mounts.csv", mapping, file_hash(mapping))]
        for key in sorted(files, key=int):
            path, metadata = self.work / "originals" / f"{key}.bin", self.project["assets"][key]
            if not path.is_file() or file_hash(path) != metadata["sha256"]:
                raise ValueError(f"Missing or altered original {key}; rerun acquisition")
            entries.append((f"EsteriaRetail\\{scope}\\{key}.bin", path, metadata["sha256"]))
        limit = bounded(self.settings.get("archive_limit_mb"), 1800, 128, 1800, "Maximum MPQ size") * 1024 * 1024
        groups, group, size = [], [], 0
        for entry in entries:
            length = entry[1].stat().st_size
            if size + length > limit and group:
                groups.append(group)
                group, size = [], 0
            group.append(entry)
            size += length
        if group:
            groups.append(group)
        storm, archives = Storm(self.settings["storm"]), []
        for index, group in enumerate(groups, 1):
            self.checkpoint()
            archive = output / f"patch-ZzzzzzzzzzzMountStudio-{index:03}.MPQ"
            emit("progress", message=f"Compressing and verifying MPQ {index}/{len(groups)}")
            storm.pack(archive, group)
            archives.append({"file": archive.name, "sha256": file_hash(archive), "size": archive.stat().st_size})
        receipt = {"schema": 1, "source": source, "archives": archives,
                   "export_mode": "completed-only" if ready_only else "all-selected",
                   "skipped_mounts": [{"display": row["display"], "name": row["name"], "status": row["status"]}
                                      for row in skipped],
                   "mounts": [{key: row.get(key) for key in ("spell", "spells", "creatures", "mount_types",
                                                             "display", "source", "advfly", "files")}
                              for row in chosen],
                   "files": {name: expected for name, _, expected in entries},
                   "evidence": "native-offline-validated-and-mpq-readback",
                   "live_acceptance": "pending", "requires": "wowdecomp MountStudio packed-reader integration"}
        save_json(output / "receipt.json", receipt)
        self.project["last_export"] = str(output)
        self.project["last_export_count"] = len(chosen)
        self.result()
        emit("progress", message=f"Exported {len(chosen)} mounts to {output}. "
             f"{len(skipped)} unfinished selections retained for resume.")
        emit("exported", path=str(output))

    def verify_baseline(self):
        providers = self.project.get("providers", [])
        order = [str(path) for path in archive_order(self.client, self.settings["locale"])]
        if not providers or order != self.project.get("archive_order"):
            raise ValueError("Client archive providers changed or no scan exists; scan again before export")
        if creature_models(self.project.get("creature_ids", []), self.settings) != self.project.get("creature_models"):
            raise ValueError("Server mount model mappings changed since scan; scan again before export")
        storm = Storm(self.settings["storm"])
        for provider in providers:
            self.checkpoint()
            path = Path(provider["path"])
            if path.suffix.lower() == ".mpq":
                with storm.archive(path) as archive:
                    data = storm.read(archive, f"DBFilesClient\\{provider['table']}.dbc")
                checksum = digest(data)
            else:
                checksum = file_hash(path)
            if checksum != provider["sha256"]:
                raise ValueError(f"{provider['table']} provider changed since scan; scan again before export")

    def doctor(self):
        if not (self.client / "Data").is_dir():
            raise ValueError("The target client folder does not contain Data; check its location in Settings")
        for key in ("probe", "storm", "reader", "definitions", "source_config"):
            path = Path(self.settings[key])
            if not path.exists():
                raise ValueError(f"Missing bundled component: {key}; reinstall Mount Studio")
            emit("progress", message=f"Available: {key}")
        Storm(self.settings["storm"])
        sys.path.insert(0, self.settings["reader"])
        from wotlkconv.db.db2 import parse_db2  # noqa: F401
        creature_models([31319], self.settings)
        self.source()
        emit("progress", message=f"Source ready: {self.project['source_info']} · "
             f"build {self.project['source']['build']}")
        emit("progress", message=f"Setup passed using Python {sys.version.split()[0]} and bundled native tools")


def main():
    request = json.load(sys.stdin)
    studio = Studio(request)
    action = sys.argv[1]
    # O_EXCL also prevents separate app windows/CLI processes racing project checkpoints and StormLib.
    lock = studio.work / "job.lock"
    try:
        fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        raise RuntimeError(f"Another job owns {lock}; after a crash, remove this file only when no worker is running")
    try:
        with os.fdopen(fd, "w") as stream:
            stream.write(str(os.getpid()))
        studio.pause.unlink(missing_ok=True)
        if action in ("save", "download", "export"):
            studio.selections(request)
        if action == "load":
            studio.load()
        elif action == "save":
            studio.result()
        elif action == "export":
            studio.export(ready_only=request.get("ready_only") is True)
        elif action in ("scan", "catalog", "download", "doctor"):
            getattr(studio, action)()
        else:
            raise ValueError("Unknown operation")
    except Paused:
        studio.result()
        emit("progress", message="Paused. Completed downloads are retained; run acquisition again to resume.")
    finally:
        lock.unlink(missing_ok=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as exception:
        emit("error", message=str(exception))
        sys.exit(1)
