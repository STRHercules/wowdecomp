import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import package as distribution


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.build = self.folder / "native"
        self.patcher = self.folder / "patcher"
        for path, content in ((self.build / "Release/BodySlider.dll", b"test dll"),
                              (self.patcher / "Release/BodySliderPatch.exe", b"test exe")):
            path.parent.mkdir(parents=True)
            path.write_bytes(content)
        self.output = self.folder / "BodySlider.zip"

    def assemble(self):
        with contextlib.redirect_stdout(io.StringIO()):
            distribution.package(self.build, self.patcher, self.output)

    def test_manifest_matches_all_files_and_no_wow_executable(self):
        self.assemble()
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            names = set(archive.namelist())
            self.assertTrue({"BodySlider.dll", "BodySliderPatch.exe", "README.md", "VALIDATION.md",
                             "Interface/AddOns/BodySlider/BodySlider.lua",
                             "Interface/AddOns/BodySlider/BodySlider.toc",
                             "Interface/AddOns/BodySlider/Protocol.lua", "SHA256.json"}.issubset(names))
            self.assertFalse(any(Path(name).name.lower() == "wow.exe" for name in names))
            self.assertFalse(any(name.endswith(".py") or "__pycache__" in name for name in names))
            manifest = json.loads(archive.read("SHA256.json"))
            self.assertEqual(set(manifest), names - {"SHA256.json"})
            for name, digest in manifest.items():
                self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(), digest)

    def test_existing_archive_is_never_overwritten(self):
        self.output.write_bytes(b"keep existing archive")
        with self.assertRaises(ValueError):
            self.assemble()
        self.assertEqual(self.output.read_bytes(), b"keep existing archive")

    def test_missing_binary_input_does_not_create_archive(self):
        (self.build / "Release/BodySlider.dll").unlink()
        with self.assertRaises(ValueError):
            self.assemble()
        self.assertFalse(self.output.exists())

    def test_lost_exclusive_creation_race_preserves_other_file(self):
        original_open = Path.open

        def racing_open(path, mode="r", *args, **kwargs):
            if path == self.output and mode == "xb":
                with original_open(path, "wb") as stream:
                    stream.write(b"other writer owns this")
                raise FileExistsError("Another process created the output")
            return original_open(path, mode, *args, **kwargs)

        with patch.object(Path, "open", racing_open), self.assertRaises(FileExistsError):
            self.assemble()
        self.assertEqual(self.output.read_bytes(), b"other writer owns this")

    def test_write_failure_removes_only_our_incomplete_archive(self):
        with patch.object(zipfile.ZipFile, "write", side_effect=OSError("simulated disk failure")):
            with self.assertRaises(OSError):
                self.assemble()
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
