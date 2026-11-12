import copy
import json
from pathlib import Path
import unittest

import model_audit


class ModelAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profiles = json.loads(Path(__file__).with_name("stock_profiles.json").read_text(encoding="utf-8"))

    def test_all_original_race_sex_rigs_present_and_consistent(self):
        models = self.profiles["models"]
        self.assertEqual(len(models), 20)
        for name, model in models.items():
            with self.subTest(model=name):
                self.assertEqual(model_audit.select_rig(model), model["rig"])
                self.assertEqual(model["rig"]["roots"], [0])
                self.assertEqual(model["rig"]["armAxis"], [2, 2])
                self.assertEqual(model["rig"]["legAxis"], [2, 2])
                self.assertEqual(len(model["key_bones"]), 27)

    def test_missing_key_arms_and_extra_elbow_on_blood_elf_male(self):
        model = self.profiles["models"]["BloodElfMale"]
        self.assertEqual(model["key_bones"][:2], [-1, -1])
        self.assertEqual(model["rig"]["arms"], [29, 30])
        self.assertEqual(model["rig"]["shoulders"], [20, 21])
        self.assertEqual(model["rig"]["lowerarms"], [47, 48])

    def test_draenei_limbs_use_audited_raw_chains(self):
        models = self.profiles["models"]
        self.assertEqual(models["DraeneiMale"]["rig"]["arms"], [31, 29])
        self.assertEqual(models["DraeneiMale"]["key_bones"][:2], [44, 32])
        self.assertEqual(models["DraeneiFemale"]["rig"]["upperlegs"], [7, 8])
        self.assertEqual(models["DraeneiFemale"]["rig"]["lowerlegs"], [15, 16])

    def test_non_mesh_event_roots_excluded(self):
        model = self.profiles["models"]["HumanMale"]
        roots = [bone for bone in model["bones"] if bone["parent"] < 0]
        self.assertGreater(len(roots), 4)
        self.assertTrue(all(not bone["weighted_vertices"] for bone in roots if bone["index"] != 0))
        self.assertEqual(model["rig"]["roots"], [0])

    def test_unknown_landmarks_rejected(self):
        model = copy.deepcopy(self.profiles["models"]["HumanMale"])
        model["bones"][36]["name_crc"] = 0
        with self.assertRaises(ValueError):
            model_audit.select_rig(model)

    def test_truncated_or_non_wotlk_model_rejected(self):
        for data in (b"", b"MD20" + b"\0" * 64, b"MD21" + b"\0" * 64):
            with self.subTest(data=data[:4]), self.assertRaises(ValueError):
                model_audit.parse_model(data)

    def test_generated_header_matches_audit(self):
        header = Path(__file__).with_name("native") / "StockProfiles.h"
        self.assertEqual(model_audit.generate_header(self.profiles), header.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
