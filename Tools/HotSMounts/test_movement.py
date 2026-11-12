import unittest

from movement import SKILL_FIELDS, SPELL_FIELDS, join, patch_skills, patch_spells, split


class MovementTests(unittest.TestCase):
    def test_flying_copies_speed_effects_and_remove_drops_rows(self):
        ground = [0] * SPELL_FIELDS
        ground[95:98], ground[110] = [78, 32, 0], 555
        rows = [[1] + ground[1:], [2] + ground[1:], [3] + ground[1:]]
        donor = [0] * SPELL_FIELDS
        donor[95:98], donor[81:83], donor[110], donor[8] = [78, 32, 207], [99, 279], 999, 0x4000000
        out, strings = split(patch_spells(join(rows, SPELL_FIELDS, b'\0x\0'), {1: 'Flying', 2: 'Remove'}, donor), SPELL_FIELDS)
        self.assertEqual([r[0] for r in out], [1, 3])
        self.assertEqual((out[0][95:98], out[0][81:83], out[0][110], out[0][8]), ([78, 32, 207], [99, 279], 555, 0x4000000))
        self.assertEqual(out[1][95:98], [78, 32, 0])
        self.assertEqual(strings, b'\0x\0')
        seated, _ = split(patch_spells(join(rows, SPELL_FIELDS, b'\0'), {}, donor, frozenset({3})), SPELL_FIELDS)
        self.assertEqual([r[131] for r in seated], [0, 0, 14418])
        skills = [[10, 777, 2] + [0] * (SKILL_FIELDS - 3), [11, 777, 3] + [0] * (SKILL_FIELDS - 3)]
        kept, _ = split(patch_skills(join(skills, SKILL_FIELDS, b'\0'), {2}), SKILL_FIELDS)
        self.assertEqual([r[0] for r in kept], [11])


if __name__ == '__main__':
    unittest.main()
