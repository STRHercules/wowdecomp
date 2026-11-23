"""Headless check that every catalogued game boots on its core the way Extensions/Arcade boots it (ROM from memory
unless the core needs a path), runs, draws non-blank frames, makes sound and round-trips a save state.

    python smoke_cores.py <cores dir> [frames]
"""
import ctypes as c
import sys
import tempfile
from pathlib import Path

from pack import CORES, GAMES

cores_dir = Path(sys.argv[1])
FRAMES = int(sys.argv[2]) if len(sys.argv) > 2 else 300
SYSTEM_DIR = c.create_string_buffer(tempfile.mkdtemp(prefix="arcade-smoke-").encode())


class SystemInfo(c.Structure):
    _fields_ = [("library_name", c.c_char_p), ("library_version", c.c_char_p), ("valid_extensions", c.c_char_p),
                ("need_fullpath", c.c_bool), ("block_extract", c.c_bool)]


class GameInfo(c.Structure):
    _fields_ = [("path", c.c_char_p), ("data", c.c_void_p), ("size", c.c_size_t), ("meta", c.c_char_p)]


class Geometry(c.Structure):
    _fields_ = [("base_width", c.c_uint), ("base_height", c.c_uint), ("max_width", c.c_uint), ("max_height", c.c_uint),
                ("aspect_ratio", c.c_float)]


class Timing(c.Structure):
    _fields_ = [("fps", c.c_double), ("sample_rate", c.c_double)]


class AvInfo(c.Structure):
    _fields_ = [("geometry", Geometry), ("timing", Timing)]


ENV = c.CFUNCTYPE(c.c_bool, c.c_uint, c.c_void_p)
VIDEO = c.CFUNCTYPE(None, c.c_void_p, c.c_uint, c.c_uint, c.c_size_t)
SAMPLE = c.CFUNCTYPE(None, c.c_int16, c.c_int16)
BATCH = c.CFUNCTYPE(c.c_size_t, c.POINTER(c.c_int16), c.c_size_t)
POLL = c.CFUNCTYPE(None)
STATE = c.CFUNCTYPE(c.c_int16, c.c_uint, c.c_uint, c.c_uint, c.c_uint)

stats = {}


@ENV
def environment(cmd, data):
    cmd &= ~0x10000
    if cmd == 10:  # SET_PIXEL_FORMAT
        stats["format"] = c.cast(data, c.POINTER(c.c_int)).contents.value
        return stats["format"] <= 2
    if cmd in (9, 31):  # GET_SYSTEM_DIRECTORY, GET_SAVE_DIRECTORY
        c.cast(data, c.POINTER(c.c_void_p))[0] = c.addressof(SYSTEM_DIR)
        return True
    if cmd in (3, 51):  # GET_CAN_DUPE, GET_INPUT_BITMASKS
        c.cast(data, c.POINTER(c.c_bool))[0] = True
        return True
    return cmd in (11, 13, 16, 34, 35, 36, 37, 44, 64, 65)


@VIDEO
def video(data, w, h, pitch):
    if not data:
        return
    stats["size"] = (w, h)
    row = c.string_at(data + (h // 2) * pitch, pitch)
    if len(set(row)) > 2:
        stats["drawn"] = stats.get("drawn", 0) + 1


@SAMPLE
def sample(l, r):
    stats["audio"] = stats.get("audio", 0) + 1


@BATCH
def batch(data, frames):
    stats["audio"] = stats.get("audio", 0) + frames
    return frames


@POLL
def poll():
    pass


@STATE
def input_state(port, device, index, button):
    return 0


failed = 0
for gid, title, system, source in GAMES:
    stats.clear()
    dll = c.CDLL(str(cores_dir / f"{CORES[system]}_libretro.dll"))
    dll.retro_get_system_info.argtypes = [c.POINTER(SystemInfo)]
    dll.retro_get_system_av_info.argtypes = [c.POINTER(AvInfo)]
    dll.retro_load_game.argtypes = [c.POINTER(GameInfo)]
    dll.retro_load_game.restype = c.c_bool
    dll.retro_serialize_size.restype = c.c_size_t
    dll.retro_serialize.argtypes = dll.retro_unserialize.argtypes = [c.c_void_p, c.c_size_t]
    dll.retro_serialize.restype = dll.retro_unserialize.restype = c.c_bool
    dll.retro_get_memory_size.restype = c.c_size_t
    dll.retro_set_environment(environment)
    dll.retro_init()
    for setter, cb in (("video_refresh", video), ("audio_sample", sample), ("audio_sample_batch", batch),
                       ("input_poll", poll), ("input_state", input_state)):
        getattr(dll, "retro_set_" + setter)(cb)
    info = SystemInfo()
    dll.retro_get_system_info(c.byref(info))
    rom = source.read_bytes()
    buf = c.create_string_buffer(rom, len(rom))
    game = GameInfo(str(source).encode(), None if info.need_fullpath else c.addressof(buf), 0 if info.need_fullpath else len(rom), None)
    ok = dll.retro_load_game(c.byref(game))
    result = "refused"
    if ok:
        av = AvInfo()
        dll.retro_get_system_av_info(c.byref(av))
        for _ in range(FRAMES):
            dll.retro_run()
        size = dll.retro_serialize_size()
        state = c.create_string_buffer(size)
        saved = size and dll.retro_serialize(state, size) and dll.retro_unserialize(state, size)
        sram = dll.retro_get_memory_size(0)
        good = stats.get("drawn", 0) > 0 and stats.get("audio", 0) > 0 and saved
        result = (f"{'ok  ' if good else 'BAD '} {stats.get('size')} fmt {stats.get('format', 0)} {av.timing.fps:.2f}fps "
                  f"{av.timing.sample_rate:.0f}Hz drawn {stats.get('drawn', 0)}/{FRAMES} audio {stats.get('audio', 0)} "
                  f"state {size}B sram {sram}B fullpath {info.need_fullpath}")
        failed += not good
        dll.retro_unload_game()
    else:
        failed += 1
    dll.retro_deinit()
    print(f"{gid:2} {title:38} {CORES[system]:16} {result}", flush=True)
    c.windll.kernel32.FreeLibrary(c.c_void_p(dll._handle))
sys.exit(1 if failed else 0)
