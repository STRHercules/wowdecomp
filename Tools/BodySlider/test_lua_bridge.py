"""Offline checks for the stock Lua callback bridge; not live-client acceptance."""

import hashlib
from pathlib import Path
import re
import struct
import unittest

import patcher


STOCK = Path("D:/Wow.exe")
HERE = Path(__file__).resolve().parent
VALIDATOR = 0x86B5A0
VALIDATOR_BYTES = bytes.fromhex(
    "558beca1b815d4008b0dbc15d40083ec4085c0740485c97510e852ffffff"
    "a1b815d4008b0dbc15d4008b55083bd072043bd1721d5268c0c4a4008d45c0"
    "6a4050e88b3af0ff8d4dc051e8b274f0ff83c4148be55dc3")
EXPECTED = (0x84DBE1, 0x84DF17, 0x84DF55, 0x84DFD1, 0x84E022, 0x84E064)


class LuaBridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import pefile
            import capstone
        except ImportError as error:
            raise unittest.SkipTest("Offline PE/disassembly checks require pefile and capstone") from error
        if not STOCK.is_file():
            raise unittest.SkipTest("Supported stock D:/Wow.exe is unavailable")
        cls.original = STOCK.read_bytes()
        if hashlib.sha256(cls.original).hexdigest() != patcher.STOCK_SHA256:
            raise unittest.SkipTest("D:/Wow.exe differs from the supported input")
        cls.pe = pefile.PE(data=cls.original)
        cls.base = cls.pe.OPTIONAL_HEADER.ImageBase
        cls.text = next(section for section in cls.pe.sections if section.Name.rstrip(b"\0") == b".text")
        cls.low = cls.base + cls.text.VirtualAddress
        cls.high = cls.low + cls.text.Misc_VirtualSize
        cls.disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        cls.disassembler.skipdata = True
        header = (HERE / "native/NativeBridge.h").read_text(encoding="utf-8")
        slots = re.search(r"\bSlots\s*\[\s*6\s*\]\s*=\s*\{([^}]+)\}", header)
        if not slots:
            raise AssertionError("NativeBridge.h must declare exactly six bridge slots")
        cls.slots = tuple(int(value, 16) for value in re.findall(r"0x[0-9A-Fa-f]+", slots.group(1)))
        cls.header = header

    def bytes_at(self, address, count):
        return self.pe.get_data(address-self.base, count)

    def test_exact_original_text_slots_are_unused_int3_after_return(self):
        self.assertEqual(self.slots, EXPECTED)
        self.assertEqual(len(set(self.slots)), 6)
        for address in self.slots:
            with self.subTest(address=hex(address)):
                self.assertTrue(self.low <= address and address+5 <= self.high)
                self.assertEqual(self.bytes_at(address-1, 6), b"\xc3" + b"\xcc"*5)
                self.assertTrue(self.text.Characteristics & 0x20000000)

    def test_six_distinct_registered_entry_names(self):
        names = re.search(r"\bNames\s*\[\s*6\s*\]\s*=\s*\{([^}]+)\}", self.header)
        self.assertIsNotNone(names)
        self.assertEqual(re.findall(r'"([^"]+)"', names.group(1)), [
            "BodySlider_Set", "BodySlider_Clear", "BodySlider_Supported",
            "BodySlider_Names", "BodySlider_HasSlider", "BodySlider_Units"])

    def test_original_file_has_no_absolute_pointer_into_bridge_slots(self):
        for slot in self.slots:
            for offset in range(5):
                with self.subTest(address=hex(slot+offset)):
                    self.assertNotIn(struct.pack("<I", slot+offset), self.original)

    def test_original_text_has_no_direct_branch_into_any_bridge_slot(self):
        # A linear scan cannot prove absence of computed jumps; it catches direct
        # decoded calls/jumps without treating inline data as invented xrefs.
        ranges = tuple((slot, slot+5) for slot in self.slots)
        hits = []
        for address, size, mnemonic, operands in self.disassembler.disasm_lite(
                self.text.get_data()[:self.text.Misc_VirtualSize], self.low):
            if mnemonic != "call" and not mnemonic.startswith("j") and not mnemonic.startswith("loop"):
                continue
            if not operands.startswith("0x"):
                continue
            try:
                destination = int(operands, 16)
            except ValueError:
                continue
            if any(start <= destination < end for start, end in ranges):
                hits.append((hex(address), mnemonic, hex(destination)))
        self.assertEqual(hits, [])

    def test_stock_validator_and_lua_callsite_remain_byte_identical(self):
        self.assertEqual(self.bytes_at(VALIDATOR, len(VALIDATOR_BYTES)), VALIDATOR_BYTES)
        # luaD_precall validates the callback, then invokes that same C function.
        call = self.bytes_at(0x856514, 9)
        self.assertEqual(call[0], 0xE8)
        self.assertEqual(0x856519 + struct.unpack_from("<i", call, 1)[0], VALIDATOR)
        self.assertEqual(call[5:9], bytes.fromhex("56ffd783"))
        patched = patcher.patch_bytes(self.original)
        offset = self.pe.get_offset_from_rva(VALIDATOR-self.base)
        self.assertEqual(patched[offset:offset+len(VALIDATOR_BYTES)], VALIDATOR_BYTES)
        for slot in self.slots:
            offset = self.pe.get_offset_from_rva(slot-self.base)
            self.assertEqual(patched[offset:offset+5], b"\xcc"*5)

    def test_validator_accepts_stock_bridges_but_rejects_dll_address(self):
        def accepted(address):
            return self.low <= address < self.high
        for slot in self.slots:
            self.assertTrue(accepted(slot))
        self.assertFalse(accepted(0x10001000))
        self.assertFalse(accepted(self.high))
        self.assertFalse(accepted(self.low-1))

    def test_emitted_rel32_jumps_reach_their_distinct_dll_callbacks(self):
        for index, slot in enumerate(self.slots):
            destination = 0x10001000 + index*0x100
            code = b"\xe9" + struct.pack("<i", destination-slot-5)
            instructions = list(self.disassembler.disasm_lite(code, slot))
            self.assertEqual(instructions, [(slot, 5, "jmp", hex(destination))])
            self.assertEqual(slot+5+struct.unpack_from("<i", code, 1)[0], destination)


if __name__ == "__main__":
    unittest.main()
