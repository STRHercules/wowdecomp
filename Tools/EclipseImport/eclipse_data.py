"""Readers for the Eclipse output tree (G:\\3.3.5a - Dev\\Eclipse) and for what the Esteria client already ships.

Eclipse formats (writers live in Eclipse-DEV):
  *.edt  EDTB DBC delta: 96-byte header, then WDBC-shaped rows and a string pool (tools/art/table_delta.py)
  mounts.edb  EMDB v2: u32 display, realm display, realm spell, name, icon, model, flags, pose (tools/mounts/mounts.py)
"""
from __future__ import annotations

import ctypes
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

ECLIPSE = Path(r"G:\3.3.5a - Dev\Eclipse")
CLIENT = Path(r"G:\3.3.5a - Dev")
SERVER_TOOLS = Path(r"R:\Users\Zach\Documents\GitHub\EsteriaWoW\tools")
RETAIL_TOOLS = Path(__file__).resolve().parent.parent / "RetailAssets"
BS = "\\"

sys.path.insert(0, str(SERVER_TOOLS))
sys.path.insert(0, str(RETAIL_TOOLS))
import cars_mount_pack as cmp  # noqa: E402  (StormLib wrapper shared with Esteria's other pack tools)


def storm():
    bundled = Path(__file__).resolve().parent.parent / "MountStudio/runtime/backend/StormLib.dll"
    dll = Path(os.environ.get("ESTERIA_STORMLIB", cmp.DLL_DEFAULT))
    if not dll.is_file():
        dll = bundled
    if not dll.is_file():
        raise SystemExit(f"StormLib DLL missing: {dll} (set ESTERIA_STORMLIB)")
    return cmp.Storm(dll)


def norm(path: str) -> str:
    return path.replace("/", BS).lower()


def stem(path: str) -> str:
    return norm(path).rsplit(BS, 1)[-1].rsplit(".", 1)[0]


def as_m2(path: str) -> str:
    return re.sub(r"\.(mdx|mdl)$", ".m2", path, flags=re.I)


def cstr(pool: bytes, offset: int) -> str:
    if not offset or offset >= len(pool):
        return ""
    return pool[offset:pool.find(b"\0", offset)].decode("utf-8", "replace")


class Edt:
    """An EDTB delta: rows keyed by ID, plus the string pool their string fields point into."""

    def __init__(self, path: Path):
        data = path.read_bytes()
        magic, version, fields, self.record_size, count, string_bytes, self.string_cols = struct.unpack_from("<4sIIIIIQ", data)
        if magic != b"EDTB" or version != 1:
            raise ValueError(f"{path} is not an EDTB v1 delta")
        self.name = data[32:96].split(b"\0")[0].decode()
        self.fields = fields
        start = 96 + count * self.record_size
        self.pool = data[start:start + string_bytes]
        if self.record_size % 4:
            raise ValueError(f"{self.name}: byte-packed rows are not supported here")
        width = self.record_size // 4
        self.rows = {}
        for i in range(count):
            row = list(struct.unpack_from(f"<{width}I", data, 96 + i * self.record_size))
            self.rows[row[0]] = row

    def text(self, offset: int) -> str:
        return cstr(self.pool, offset)


def eclipse_mounts() -> list[dict]:
    data = (ECLIPSE / "data/mounts/mounts.edb").read_bytes()
    magic, version, count, string_bytes, _stamp = struct.unpack_from("<4s4I", data)
    if magic != b"EMDB" or version != 2:
        raise ValueError("mounts.edb is not an EMDB v2 catalogue")
    pool = data[20 + count * 32:]
    result = []
    for i in range(count):
        display, source, spell, name, icon, model, flags, pose = struct.unpack_from("<8I", data, 20 + i * 32)
        result.append({"display": display, "realm_display": source, "realm_spell": spell, "name": cstr(pool, name),
                       "icon": cstr(pool, icon), "model": cstr(pool, model), "pose": pose})
    return result


class Archives:
    """Every MPQ the Esteria client mounts, opened read-only once."""

    def __init__(self, client: Path = CLIENT):
        self.storm = storm()
        data = client / "Data"
        paths = [p for p in data.iterdir() if p.suffix.lower() == ".mpq"]
        for locale in data.iterdir():
            if locale.is_dir() and re.fullmatch(r"[a-z]{2}[A-Z]{2}", locale.name):
                paths += [p for p in locale.iterdir() if p.suffix.lower() == ".mpq"]
        self.paths = sorted(paths, key=lambda p: p.name.lower())
        self.handles = {p: self.storm.open_archive(p) for p in self.paths}
        self._names = None
        self._listed = {}

    def close(self):
        for handle in self.handles.values():
            self.storm.dll.SFileCloseArchive(handle)

    def listed(self, path: Path) -> list[str]:
        if path not in self._listed:
            self._listed[path] = list_files(self.storm, self.handles[path])
        return self._listed[path]

    def names(self) -> set[str]:
        """Every listed path in the client. Archives without a (listfile) are covered by has()."""
        if self._names is None:
            self._names = {norm(name) for path in self.paths for name in self.listed(path)}
        return self._names

    def has(self, name: str) -> bool:
        if norm(name) in self.names():
            return True
        key = name.replace("/", BS).encode("ascii", "replace")
        for handle in self.handles.values():
            file_handle = cmp.H()
            if self.storm.dll.SFileOpenFileEx(handle, key, 0, ctypes.byref(file_handle)):
                self.storm.dll.SFileCloseFile(file_handle)
                return True
        return False

    def tables(self, name: str) -> dict[int, tuple[list[int], bytes]]:
        """Union of a DBC's rows across all archives (and their .dbc1-* continuations), later archive names winning."""
        rows = {}
        prefix = norm(f"DBFilesClient\\{name}.dbc")
        for path, handle in self.handles.items():
            for listed in self.listed(path):
                if not norm(listed).startswith(prefix):
                    continue
                payload = self.storm.read(handle, listed)
                if payload[:4] != b"WDBC":
                    continue
                _, count, fields, record_size, _ = struct.unpack_from("<4s4I", payload)
                if record_size != fields * 4:
                    continue
                pool = payload[20 + count * record_size:]
                for i in range(count):
                    row = list(struct.unpack_from(f"<{fields}I", payload, 20 + i * record_size))
                    rows[row[0]] = (row, pool)
        return rows


def list_files(storm, handle) -> list[str]:
    """cars_mount_pack.Storm.list_files, but tolerant of the Latin-1 names some Esteria archives carry."""
    data = cmp.FindData()
    finder = storm.dll.SFileFindFirstFile(handle, b"*", ctypes.byref(data), None)
    if not finder:
        return []
    result = []
    try:
        while True:
            result.append(data.cFileName.split(b"\0", 1)[0].decode("latin1"))
            if not storm.dll.SFileFindNextFile(finder, ctypes.byref(data)):
                break
    finally:
        storm.dll.SFileFindClose(finder)
    return result


class Icons:
    """wardrobe/icons.pak (EICO v1): BC2/DXT3 atlas pages of square icon cells (src/eclipse/art/Icons.cpp)."""

    def __init__(self, path: Path = ECLIPSE / "data/wardrobe/icons.pak"):
        self.data = path.read_bytes()
        magic, version, self.cell, self.page, entries, pages, name_bytes = struct.unpack_from("<4s6I", self.data)
        if magic != b"EICO" or version != 1 or self.cell % 4 or self.page % self.cell:
            raise ValueError("icons.pak is not an EICO v1 pack")
        names = self.data[28 + entries * 8: 28 + entries * 8 + name_bytes]
        self.page_bytes = (self.page // 4) ** 2 * 16
        self.pages_at = 28 + entries * 8 + name_bytes
        if len(self.data) != self.pages_at + pages * self.page_bytes:
            raise ValueError("icons.pak size does not match its header")
        self.entries = {}
        for i in range(entries):
            name, page, cell = struct.unpack_from("<IHH", self.data, 28 + i * 8)
            self.entries[cstr(names, name).lower()] = (page, cell)

    def __contains__(self, name: str) -> bool:
        return name.lower() in self.entries

    def blp(self, name: str) -> bytes:
        """The icon as a one-mip DXT3 BLP2, the format the client's icon textures already use."""
        page, cell = self.entries[name.lower()]
        per_row = self.page // self.cell
        blocks, row_blocks = self.cell // 4, self.page // 4
        x0, y0 = (cell % per_row) * blocks, (cell // per_row) * blocks
        base = self.pages_at + page * self.page_bytes
        pixels = b"".join(self.data[base + ((y0 + y) * row_blocks + x0) * 16: base + ((y0 + y) * row_blocks + x0 + blocks) * 16]
                          for y in range(blocks))
        offsets, sizes = [1172] + [0] * 15, [len(pixels)] + [0] * 15
        header = b"BLP2" + struct.pack("<I4B2I", 1, 2, 8, 1, 0, self.cell, self.cell)
        return header + struct.pack("<16I", *offsets) + struct.pack("<16I", *sizes) + bytes(1024) + pixels


def world_query(sql: str) -> list[list[str]]:
    from server import query
    return [line.split("\t") for line in query(sql).splitlines() if line]


def m2_textures(data: bytes) -> list[str]:
    return [name.decode("latin1") for _, kind, name in cmp._m2_texture_records(data) if kind == 0 and name]


def m2_sequences(data: bytes) -> list[int]:
    count, offset = struct.unpack_from("<II", data, 0x1C)
    return [struct.unpack_from("<H", data, offset + i * 0x40)[0] for i in range(count)]


def m2_external_anims(model_path: str, data: bytes) -> list[str]:
    """3.3.5 keeps non-inline sequences in <model><animId>-<subId>.anim (sequence flag 0x20 = inline)."""
    count, offset = struct.unpack_from("<II", data, 0x1C)
    base = norm(model_path).rsplit(".", 1)[0]
    result = []
    for i in range(count):
        anim, sub = struct.unpack_from("<HH", data, offset + i * 0x40)
        flags = struct.unpack_from("<I", data, offset + i * 0x40 + 0x0C)[0]
        if not flags & 0x60:  # 0x20 data inline, 0x40 alias of another sequence
            result.append(f"{base}{anim:04d}-{sub:02d}.anim")
    return result


def m2_skins(model_path: str, data: bytes) -> list[str]:
    views = struct.unpack_from("<I", data, 0x44)[0]
    base = norm(model_path).rsplit(".", 1)[0]
    return [f"{base}{i:02d}.skin" for i in range(views)]


if __name__ == "__main__":
    # self-check against the shipped files
    mounts = eclipse_mounts()
    cdi = Edt(ECLIPSE / "data/tables/CreatureDisplayInfo.edt")
    cmd = Edt(ECLIPSE / "data/tables/CreatureModelData.edt")
    assert len(mounts) > 1000 and all(m["display"] in cdi.rows for m in mounts[:50])
    assert cdi.fields == 16 and cmd.fields == 28
    print(len(mounts), "mounts;", len(cdi.rows), "displays;", len(cmd.rows), "models")
