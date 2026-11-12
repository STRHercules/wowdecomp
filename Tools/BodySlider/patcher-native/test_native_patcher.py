import contextlib
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import patcher
import generate_patch_data

EXE = Path(os.environ.get("BODYSLIDER_PATCH_EXE", HERE.parents[2] / "build/body-slider-patcher/Release/BodySliderPatch.exe"))
STOCK = Path("D:/Wow.exe")


class NativePatcherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not EXE.is_file() or not STOCK.is_file():
            raise unittest.SkipTest("Build the native patcher and provide the supported D:/Wow.exe")
        cls.original = STOCK.read_bytes()
        if patcher.sha256(cls.original) != patcher.STOCK_SHA256:
            raise unittest.SkipTest("D:/Wow.exe no longer matches the supported executable")
        cls.patched = patcher.patch_bytes(cls.original)

    def run_cli(self, *args, success=True):
        result = subprocess.run([str(EXE), *map(str, args)], capture_output=True, timeout=15)
        self.assertEqual(result.returncode == 0, success, result.stdout.decode("utf-8", "replace") + result.stderr.decode("utf-8", "replace"))
        return result

    def test_help_without_gui(self):
        self.assertIn(b"--patch", self.run_cli("--help").stdout)

    def test_native_output_identical_to_python_and_both_restore(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "patched.exe"
            restored = Path(folder) / "restored.exe"
            self.run_cli("--patch", STOCK, output)
            self.assertEqual(output.read_bytes(), self.patched)
            self.assertEqual(patcher.restore_bytes(output.read_bytes()), self.original)
            self.run_cli("--restore", output, restored)
            self.assertEqual(restored.read_bytes(), self.original)

    def test_python_patch_restored_by_native(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "python-patched.exe"
            output = Path(folder) / "original.exe"
            source.write_bytes(self.patched)
            self.run_cli("--restore", source, output)
            self.assertEqual(output.read_bytes(), self.original)

    def test_unicode_paths(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder) / "角色 modèles"
            directory.mkdir()
            source, output = directory / "魔兽.exe", directory / "patched 原本.exe"
            source.write_bytes(self.original)
            self.run_cli("--patch", source, output)
            self.assertEqual(output.read_bytes(), self.patched)
            self.assertEqual(source.read_bytes(), self.original)

    def test_no_overwrite_or_source_modification(self):
        with tempfile.TemporaryDirectory() as folder:
            source, output = Path(folder) / "stock.exe", Path(folder) / "keep.exe"
            source.write_bytes(self.original)
            output.write_bytes(b"keep")
            self.run_cli("--patch", source, output, success=False)
            self.run_cli("--patch", source, source, success=False)
            self.assertEqual(source.read_bytes(), self.original)
            self.assertEqual(output.read_bytes(), b"keep")

    def test_bad_digest_bad_size_and_wrong_operation_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            source, output = Path(folder) / "source.exe", Path(folder) / "output.exe"
            changed = bytearray(self.original)
            changed[0x1000] ^= 1
            for data in (b"MZ", self.original[:-1], changed, self.patched):
                source.write_bytes(data)
                self.run_cli("--patch", source, output, success=False)
                self.assertFalse(output.exists())
            source.write_bytes(self.original)
            self.run_cli("--restore", source, output, success=False)
            self.assertFalse(output.exists())

    def test_tampered_patched_copy_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            source, output = Path(folder) / "changed.exe", Path(folder) / "restored.exe"
            for offset in (0x1000, 0x58, len(self.patched)-1):
                changed = bytearray(self.patched)
                changed[offset] ^= 1
                source.write_bytes(changed)
                self.run_cli("--restore", source, output, success=False)
                self.assertFalse(output.exists())

    def test_embedded_data_regenerates_exactly(self):
        with contextlib.redirect_stdout(io.StringIO()):
            header = generate_patch_data.generate(STOCK)
        self.assertEqual((HERE / "PatchData.h").read_text(encoding="ascii"), header)
        self.assertEqual(hashlib.sha256(STOCK.read_bytes()).hexdigest(), patcher.STOCK_SHA256)

    def test_tiny_and_only_system_imports(self):
        self.assertLess(EXE.stat().st_size, 200 * 1024)
        if importlib.util.find_spec("pefile"):
            import pefile
            binary = pefile.PE(str(EXE))
            self.assertEqual(binary.FILE_HEADER.Machine, 0x14C)
            system = {b"KERNEL32.dll", b"USER32.dll", b"ADVAPI32.dll", b"SHELL32.dll", b"COMDLG32.dll"}
            self.assertLessEqual({item.dll.upper() for item in binary.DIRECTORY_ENTRY_IMPORT}, {item.upper() for item in system})


if __name__ == "__main__":
    unittest.main()
