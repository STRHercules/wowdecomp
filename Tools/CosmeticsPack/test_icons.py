import io
from pathlib import Path
import tempfile
import unittest

from PIL import Image
import backpack_icons
from stage_icons import patch_displays
import stage


class IconTests(unittest.TestCase):
    def test_exact_names_are_not_fuzzy_variants(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/"T-Icon-Backpacks-Male-Commando-10.png").touch()
            (root/"T-Icon-Backpacks-Male-Commando-10-Female.png").touch()
            path,_=backpack_icons.resolve({"source":"Male_Commando_10.uemodel"},root)
            self.assertEqual(path.name,"T-Icon-Backpacks-Male-Commando-10.png")
            self.assertIsNone(backpack_icons.resolve({"source":"SK_F_MED_Heist_Backpack.uemodel"},root)[0])

    def test_alpha_and_mips_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"icon.png"
            image=Image.new("RGBA",(8,8),(20,40,60,0));image.putpixel((3,3),(50,70,90,193));image.save(path)
            blob=backpack_icons.blp_bytes(path)
            import struct
            offset=struct.unpack_from("<I",blob,20)[0]
            decoded=Image.frombytes("RGBA",(8,8),blob[offset:offset+8*8*4],"raw","BGRA")
            self.assertEqual(decoded.tobytes(),image.tobytes())
            self.assertEqual(struct.unpack_from("<I",blob,84+3*4)[0],4)

    def test_only_expected_icon_field_changes(self):
        rows=[[0]*25 for _ in range(2)];rows[0][0]=7;rows[1][0]=8
        original=stage.native.make_wdbc(25,rows,{(0,5):"old",(1,5):"unchanged"})
        changed=patch_displays(original,{7:"Interface\\Icons\\actual"})
        before,_=stage.native.wdbc(original,25);after,pool=stage.native.wdbc(changed,25)
        self.assertEqual(before[1],after[1])
        self.assertEqual(pool[after[0][5]:].split(b"\0")[0],b"actual")
        with self.assertRaises(ValueError):patch_displays(original,{9:"bad"})


if __name__=="__main__":unittest.main()
