"""Read-only bridge/protection verification for an isolated running stock client."""

import argparse
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import re
import struct

import pefile


class MemoryInfo(c.Structure):
    _fields_ = [("base", c.c_void_p), ("allocation", c.c_void_p), ("allocation_protect", w.DWORD),
                ("region_size", c.c_size_t), ("state", w.DWORD), ("protect", w.DWORD), ("type", w.DWORD)]


def verify(pid, original):
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    kernel.VirtualQueryEx.argtypes = [w.HANDLE, c.c_void_p, c.POINTER(MemoryInfo), c.c_size_t]
    kernel.VirtualQueryEx.restype = c.c_size_t
    kernel.CloseHandle.argtypes = [w.HANDLE]
    process = kernel.OpenProcess(0x410, False, pid)
    if not process:
        raise c.WinError(c.get_last_error())
    try:
        def read(address, size):
            data, count = c.create_string_buffer(size), c.c_size_t()
            if not kernel.ReadProcessMemory(process, address, data, size, c.byref(count)) or count.value != size:
                raise c.WinError(c.get_last_error())
            return data.raw

        pe = pefile.PE(str(original))
        low, high = struct.unpack("<II", read(0xD415B8, 8))
        assert (low, high) == (0x401000, 0x9DE3B3), "Unexpected stock Lua text bounds"
        for address, length in ((0x86B5A0, 85), (0x4046C0, 32)):
            assert read(address, length) == pe.get_data(address-0x400000, length), "Stock validator/check code changed"
        header = (Path(__file__).parent / "native" / "NativeBridge.h").read_text()
        slots = [int(value, 16) for value in re.findall(r"0x[0-9A-Fa-f]+", header)]
        assert len(slots) == 6
        targets = set()
        for slot in slots:
            code = read(slot, 5)
            assert low <= slot < high and code[0] == 0xE9
            target = (slot+5+struct.unpack_from("<i", code, 1)[0]) & 0xFFFFFFFF
            region = MemoryInfo()
            assert kernel.VirtualQueryEx(process, slot, c.byref(region), c.sizeof(region))
            assert region.protect == 0x20, "Bridge page was not restored to execute/read"
            assert read(target, 1) not in (b"\0", b"\xcc"), "Invalid bridge destination"
            targets.add(target)
            print(f"0x{slot:08X} -> 0x{target:08X}, stock validation accepts entry; page RX")
        assert len(targets) == 6
        print("Six live bridges verified; stock Lua validator and check entry unchanged.")
    finally:
        kernel.CloseHandle(process)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pid", type=int)
    parser.add_argument("--original", type=Path, default=Path("D:/Wow.exe"))
    args = parser.parse_args()
    verify(args.pid, args.original)
