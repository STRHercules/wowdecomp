"""Exercise standalone packed reads with loose originals unavailable, including precedence and failures."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "MountStudio"))
from worker import Storm, file_hash
from pack_cache import package

PROBE = sys.argv.pop(1)
DLL = Path(os.environ["STORMLIB_DLL"])
BUILD = "0123456789abcdef0123456789abcdef"


class PackedTests(unittest.TestCase):
    def test_base_export_cannot_replace_installed_chunks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Data").mkdir()
            (root / "Data/patch-Retail-001.MPQ").write_bytes(b"existing base")
            with self.assertRaisesRegex(ValueError, "Base Retail MPQs already installed"):
                package(root, root / "new-export", DLL, root / "unused-audit.exe")
            self.assertFalse((root / "new-export").exists())

    def fixture(self, directory, product="wow"):
        client = Path(directory)
        data = client / "Data"
        config = data / "Retail/source.ini"
        config.parent.mkdir(parents=True)
        config.write_text(f"mode=files\npath=missing-loose\nproduct={product}\nbuild={BUILD}\nlocale=2\n")
        shutil.copy2(DLL, client / "StormLib.dll")
        return client, data, config

    def pack(self, root, name, payload, product="wow"):
        source = root / (name + ".bin")
        source.write_bytes(payload)
        Storm(DLL).pack(root / "Data" / name,
                        [(f"EsteriaRetail\\{product}-{BUILD}\\2\\42.bin", source, file_hash(source))])
        return source

    def probe(self, config, expected, kind="retail"):
        subprocess.run([PROBE, str(config), f"{kind}/42.m2", str(expected)], check=True, timeout=30)

    def test_read_precedence_and_no_acquisition(self):
        with tempfile.TemporaryDirectory() as directory:
            root, data, config = self.fixture(directory)
            self.pack(root, "patch-A.MPQ", b"old original")
            newest = self.pack(root, "patch-Z.MPQ", b"winning packed original")
            self.probe(config, newest)
            self.assertFalse((config.parent / "missing-loose").exists())

    def test_forever_namespace(self):
        with tempfile.TemporaryDirectory() as directory:
            root, data, config = self.fixture(directory, "wow_classic_beta")
            self.pack(root, "patch-Retail.MPQ", b"wrong product")
            wanted = self.pack(root, "patch-Forever.MPQ", b"forever original", "wow_classic_beta")
            self.probe(config, wanted, "forever")

    def test_empty_original(self):
        with tempfile.TemporaryDirectory() as directory:
            root, data, config = self.fixture(directory)
            wanted = self.pack(root, "patch-Retail.MPQ", b"")
            self.probe(config, wanted)

    def test_corrupt_archive_fails_without_reacquisition(self):
        with tempfile.TemporaryDirectory() as directory:
            root, data, config = self.fixture(directory)
            (data / "patch-Broken.MPQ").write_bytes(b"corrupt archive")
            self.probe(config, "-")
            self.assertFalse((config.parent / "missing-loose").exists())

    def test_missing_library_fails_without_reacquisition(self):
        with tempfile.TemporaryDirectory() as directory:
            root, data, config = self.fixture(directory)
            self.pack(root, "patch-Retail.MPQ", b"original")
            (root / "StormLib.dll").unlink()
            self.probe(config, "-")
            self.assertFalse((config.parent / "missing-loose").exists())


if __name__ == "__main__":
    unittest.main()
