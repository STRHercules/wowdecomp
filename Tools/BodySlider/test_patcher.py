import hashlib
from pathlib import Path
import struct
import tempfile
import unittest

import patcher


STOCK = Path("D:/Wow.exe")


class PatcherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not STOCK.is_file():
            raise unittest.SkipTest("Supplied D:/Wow.exe is not available")
        cls.original = STOCK.read_bytes()
        if patcher.sha256(cls.original) != patcher.STOCK_SHA256:
            raise unittest.SkipTest("D:/Wow.exe no longer matches the supplied stock executable")
        cls.patched = patcher.patch_bytes(cls.original)

    def test_restore_is_byte_identical(self):
        self.assertEqual(patcher.restore_bytes(self.patched), self.original)
        self.assertEqual(patcher.sha256(STOCK.read_bytes()), patcher.STOCK_SHA256)

    def test_unknown_or_already_patched_input_rejected(self):
        for candidate in (b"MZ", self.original[:-1], self.patched):
            with self.subTest(size=len(candidate)), self.assertRaises(ValueError):
                patcher.patch_bytes(candidate)

    def test_malformed_layout_rejected(self):
        for candidate in (b"", b"MZ" + b"\0" * 62, self.original[:900]):
            with self.subTest(size=len(candidate)), self.assertRaises(ValueError):
                patcher.pe_layout(candidate)

    def test_tampered_patched_file_rejected(self):
        changed = bytearray(self.patched)
        changed[0x1000] ^= 1
        with self.assertRaisesRegex(ValueError, "has changed"):
            patcher.restore_bytes(changed)
        with self.assertRaises(ValueError):
            patcher.restore_bytes(self.patched[:-1])

    def test_output_never_overwrites_source_or_existing_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            existing = Path(temporary) / "existing.exe"
            existing.write_bytes(b"keep")
            with self.assertRaises(ValueError):
                patcher.write_copy(STOCK, existing)
            with self.assertRaises(ValueError):
                patcher.write_copy(STOCK, STOCK)
            self.assertEqual(existing.read_bytes(), b"keep")

    def test_files_round_trip(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "Wow-BodySlider.exe"
            restored = Path(temporary) / "Wow-original.exe"
            patcher.write_copy(STOCK, output)
            patcher.write_copy(output, restored, restore=True)
            self.assertEqual(restored.read_bytes(), self.original)

    def test_original_code_and_resources_unchanged(self):
        original_layout = patcher.pe_layout(self.original)
        header_size = original_layout["headers_size"]
        self.assertEqual(self.patched[header_size:len(self.original)], self.original[header_size:])
        layout = patcher.pe_layout(self.patched)
        self.assertEqual(layout["count"], original_layout["count"] + 1)
        self.assertEqual(self.patched[layout["optional"] + 128:layout["optional"] + 136], b"\0" * 8)

    def test_pe_parser_and_stub_disassembly(self):
        try:
            import capstone
            import pefile
        except ImportError:
            self.skipTest("Optional pefile/capstone verification packages are not installed")
        original = pefile.PE(data=self.original)
        patched = pefile.PE(data=self.patched)
        self.assertEqual(patched.OPTIONAL_HEADER.CheckSum, patched.generate_checksum())
        self.assertEqual(patched.FILE_HEADER.NumberOfSections, 7)
        self.assertEqual(patched.sections[-1].Name, patcher.SECTION_NAME)
        self.assertEqual(patched.OPTIONAL_HEADER.AddressOfEntryPoint, patched.sections[-1].VirtualAddress)
        self.assertEqual(patched.DIRECTORY_ENTRY_IMPORT[0].dll, b"KERNEL32.dll")
        self.assertEqual([(d.dll, [(i.name, i.address) for i in d.imports]) for d in patched.DIRECTORY_ENTRY_IMPORT],
                         [(d.dll, [(i.name, i.address) for i in d.imports]) for d in original.DIRECTORY_ENTRY_IMPORT])
        section = patched.sections[-1]
        code = section.get_data()[:section.Misc_VirtualSize]
        instructions = list(capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32).disasm(
            code, patcher.IMAGE_BASE + section.VirtualAddress))
        self.assertEqual(sum(item.size for item in instructions), len(code))
        self.assertEqual(instructions[0].mnemonic, "pushfd")
        self.assertEqual(instructions[1].mnemonic, "pushal")
        self.assertEqual(instructions[-2].mnemonic, "popfd")
        self.assertEqual(instructions[-1].mnemonic, "jmp")
        self.assertEqual(instructions[-1].op_str, hex(patcher.IMAGE_BASE + original.OPTIONAL_HEADER.AddressOfEntryPoint))
        boundaries = {item.address for item in instructions}
        for item in instructions[:-1]:
            if item.mnemonic.startswith("j"):
                self.assertIn(int(item.op_str, 16), boundaries)
        calls = [item.op_str for item in instructions if item.mnemonic == "call"]
        self.assertEqual(calls, ["dword ptr [0x9df1a0]", "dword ptr [0x9df248]", "eax"])


if __name__ == "__main__":
    unittest.main()
