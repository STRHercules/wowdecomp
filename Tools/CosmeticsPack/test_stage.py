import unittest
import tempfile
from pathlib import Path
from unittest.mock import patch
import stage


class StageTests(unittest.TestCase):
    def test_exported_fits_cover_all_displays_and_generate_shared_ui_defaults(self):
        fits = stage.default_fits()
        self.assertEqual(len(fits), 99)
        self.assertEqual(fits[3400001], (1.43, 0, 0, 0.035))
        self.assertEqual(fits[3400099], (1.69, 0, 0, 0.14))
        row = {"slug":"pack", "name":"Pack", "model":"custom/fortnite/pack.m2", "width":1, "height":1, "depth":1}
        ids = {"pack":{"source":3400001,"token":3410001,"display":3400001}}
        generated, _ = stage.generated_lua([row], ids, [])
        self.assertIn("fitScale=1.43", generated)
        self.assertIn("fitZ=0.035", generated)

    def test_default_fit_import_rejects_partial_duplicate_and_invalid_profiles(self):
        source = stage.DEFAULT_FITS.read_text()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fits.txt"
            for invalid in (source.splitlines()[1], source + source.splitlines()[1] + "\n",
                            source.replace("1.430000", "nan", 1), source.replace("1.430000", "4.01", 1)):
                path.write_text(invalid)
                with patch.object(stage, "DEFAULT_FITS", path), self.assertRaises(ValueError):
                    stage.default_fits()

    def test_stable_ids_survive_new_sort_order_and_missing_sources(self):
        first=stage.allocate([{"slug":"z"},{"slug":"m"}],{})
        second=stage.allocate([{"slug":"a"},{"slug":"z"}],first)
        self.assertEqual(second["z"],first["z"])
        self.assertEqual(second["m"],first["m"])
        self.assertEqual(second["a"]["index"],3)

    def test_registry_append_preserves_old_records_pool_and_rebases_new_strings(self):
        old=stage.native.make_wdbc(2,[[7,0]],{(0,1):"old"})
        addition=stage.native.make_wdbc(2,[[8,0]],{(0,1):"new"})
        merged=stage.merge_registry(old,addition,2,(1,))
        rows,pool=stage.native.wdbc(merged,2)
        self.assertEqual(merged[20:28],old[20:28])
        self.assertEqual(pool[rows[0][1]:].split(b"\0")[0],b"old")
        self.assertEqual(pool[rows[1][1]:].split(b"\0")[0],b"new")
        with self.assertRaises(ValueError): stage.merge_registry(old,old,2,(1,))

    def test_token_and_source_are_distinct_and_dummy_spell_is_harmless(self):
        row={"slug":"shield","name":"Shield","model":"custom/fortnite/shield.m2","width":1,"height":1,"depth":0.1}
        ids=stage.allocate([row],{})
        tables=stage.build_tables([row],ids,"cape")
        items,_=stage.native.wdbc(tables["Item"],8)
        self.assertNotEqual(items[0][0],items[1][0])
        self.assertEqual(items[0][6],16)
        self.assertEqual(items[1][6],0)
        spells,_=stage.native.wdbc(tables["Spell"],234)
        self.assertEqual(spells[0][71:74],[3,0,0])
        self.assertEqual(spells[0][86],1)


if __name__=="__main__": unittest.main()
