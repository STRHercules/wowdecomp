import argparse
import json
import math
from pathlib import Path
import struct
import unittest

import deform_audit as audit
from model_audit import Archives

ROOT = Path(__file__).resolve().parent


class DeformationAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profiles = json.loads((ROOT/"deform_profiles.json").read_text())

    def test_twenty_models_and_complete_regions(self):
        self.assertEqual(len(self.profiles["models"]), 20)
        for model in self.profiles["models"].values():
            self.assertEqual(set(model["regions"]), set(audit.REGIONS))
            self.assertTrue(all(v >= 3 for v in model["surface_vertex_counts"].values()))
            self.assertEqual(len(model["variants"]), len(set(v["kind"] for v in model["variants"])))
            for zone in model["regions"].values():
                self.assertTrue(all(math.isfinite(v) for v in zone["center"]+zone["radius"]))
                self.assertGreater(min(zone["radius"]), 0)

    def test_generated_header_matches(self):
        self.assertEqual((ROOT/"native/DeformProfiles.h").read_text(), audit.generate_header(self.profiles))

    def test_fingerprint_detects_position_weight_normal_and_uv(self):
        raw = bytearray(48)
        before = audit.fingerprint(raw)
        for index in (0,12,16,20,32,47):
            raw[index] = 1
            self.assertNotEqual(before,audit.fingerprint(raw))
            raw[index] = 0

    def test_malformed_tables_rejected(self):
        with self.assertRaises((ValueError,struct.error)): audit.read_vertices(b"\0"*100)
        with self.assertRaises(ValueError): audit.read_skin(b"\0"*100,10)
        with self.assertRaises(ValueError): audit.remapped_vertices(bytes(48),dict(indices=[0],bones=b"\1\0\0\0",sections=[dict(start=0,count=1,combo=0,influences=1)]),[])

    def test_software_variants_and_explicit_rejection(self):
        variants = [v for m in self.profiles["models"].values() for v in m["variants"]]
        self.assertGreater(len(variants),60)
        self.assertTrue(all(v["count"] > 0 and len(v["fingerprint"]) == 16 for v in variants))
        rejected = self.profiles["models"]["HumanFemale"].get("unsupported_variants",[])
        self.assertTrue(rejected)
        self.assertIn("Invalid bone combo", rejected[0]["reason"])


def fixtures(output):
    output.mkdir(parents=True,exist_ok=True)
    archives = Archives(Path("D:/ChromieCraft_3.3.5a"),ROOT.parent/"MountStudio/runtime/backend/StormLib.dll")
    try:
        profiles = json.loads((ROOT/"deform_profiles.json").read_text())
        for i,model in enumerate(profiles["models"].values()):
            _,data = archives.read(model["path"].replace("/","\\"))
            raw = audit.read_vertices(data)
            (output/f"{i}.vertices").write_bytes(struct.pack("<I",len(raw)//48)+raw)
    finally:
        archives.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesh-fixtures",type=Path)
    args,rest = parser.parse_known_args()
    if args.mesh_fixtures:
        fixtures(args.mesh_fixtures)
    else:
        unittest.main(argv=[__file__]+rest)
