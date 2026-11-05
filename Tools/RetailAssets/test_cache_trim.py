"""Check removal boundaries and the integrity gate using disposable fixtures."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

TRIM = sys.argv.pop(1)


class TrimTests(unittest.TestCase):
    def fixture(self, directory):
        root = pathlib.Path(directory) / "Data/Retail/Cache"
        build = root / ("Retail/wow-" + "11" * 16)
        original = build / "decoded/2/strict/id-1.bin"
        original.parent.mkdir(parents=True)
        original.write_bytes(struct.pack("<IIQ", 0x31434457, 0, 14695981039346656037))
        archive = build / "data/22/22" / ("22" * 16)
        archive.parent.mkdir(parents=True)
        archive.write_bytes(b"unrelated archive data")
        footer = bytearray(28)
        footer[8:16] = bytes([1, 0, 0, 4, 4, 4, 16, 8])
        index = archive.with_name(archive.name + ".index")
        index.write_bytes(footer)
        loose = archive.with_name("22" * 15 + "33")
        loose.write_bytes(b"required standalone encoded file")
        return root, original, archive, index, loose

    def test_dry_run_and_targeted_removal(self):
        with tempfile.TemporaryDirectory() as directory:
            root, original, archive, index, loose = self.fixture(directory)
            subprocess.run([TRIM, str(root)], check=True, capture_output=True)
            self.assertTrue(archive.exists())
            subprocess.run([TRIM, str(root), "--apply"], check=True, capture_output=True)
            self.assertFalse(archive.exists())
            for retained in (original, index, loose):
                self.assertTrue(retained.exists())

    def test_corrupt_original_prevents_all_removal(self):
        with tempfile.TemporaryDirectory() as directory:
            root, original, archive, _, _ = self.fixture(directory)
            original.write_bytes(b"corrupt")
            result = subprocess.run([TRIM, str(root), "--apply"], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(archive.exists())


if __name__ == "__main__":
    unittest.main()
