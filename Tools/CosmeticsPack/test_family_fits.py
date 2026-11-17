import unittest
from import_family_fits import parse

class FamilyFitTests(unittest.TestCase):
    def test_only_wolf_row_is_imported_from_all_backpack_fits(self):
        axes='3430001;retail/a.m2;1;0;.035;-.085;180;0;0;drawn\n3430001;retail/a.m2;1;.29;-.06;.06;0;177.5;0;sheathed\n'
        pets='3400001;retail/a.m2;4;2;2;2\n3400100;retail/a.m2;1.36;.37;0;.45\n3400002;retail/a.m2;3;1;1;1\n'
        result=parse(axes,pets)
        self.assertEqual(result['pet_backpacks'],[1.36,.37,0,.45])
        self.assertEqual(result['fortnite_2h_axes']['drawn'],[1,0,.035,-.085,180,0,0])
        with self.assertRaises(ValueError):parse(axes,pets+pets)
        with self.assertRaises(ValueError):parse(axes.splitlines()[0],pets)

if __name__=='__main__':unittest.main()
