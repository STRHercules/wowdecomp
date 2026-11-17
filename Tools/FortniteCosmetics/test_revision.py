import json
from pathlib import Path
import unittest

from revise_collection import parse_fits,TYPES,POLICY

class RevisionTests(unittest.TestCase):
    def test_duplicate_body_defaults_must_agree(self):
        text='3400100;retail/a.m2;1.36;.37;0;.45\n3400100;retail/b.m2;1.36;.37;0;.45\n'
        self.assertEqual(parse_fits(text,False),{(3400100,'backpack'):[1.36,.37,0,.45]})
        with self.assertRaises(ValueError):parse_fits(text.replace('b.m2;1.36','b.m2;1.5'),False)
        with self.assertRaises(ValueError):parse_fits(text.replace('.45','nan'),False)
        self.assertEqual(parse_fits('3400100;a.m2;1;.37;0;.45;15;-30;90',False)[3400100,'backpack'][-3:],[15,-30,90])

    def test_weapon_states_and_policy_cover_hand_and_animation_types(self):
        fit=parse_fits('3430001;retail/a.m2;1.26;.25;.035;-.085;180;0;0;drawn\n',True)
        self.assertEqual(fit[3430001,'drawn'][0],1.26)
        with self.assertRaises(ValueError):parse_fits('3430001;a;1;0;0;0;181;0;0;drawn',True)
        self.assertEqual(TYPES['Fist (Left)'],(13,22,7,16))
        self.assertEqual(TYPES['Fist (Right)'],(13,21,7,15))
        self.assertEqual(TYPES['Polearm'][:2],(6,17))
        policy=json.loads(POLICY.read_text())
        self.assertEqual(len(set(policy['removed'])),23)
        self.assertTrue(set(policy['removed']).isdisjoint(map(int,policy['types'])))

if __name__=='__main__':unittest.main()
