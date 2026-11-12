import unittest

from client_mounts import pose_rows, shift_seats


class ClientMountTests(unittest.TestCase):
    def test_rider_rows(self):
        report = dict(models=[dict(name='SpectreBike', category='Ridebike', displays=[dict(hots_id='SpectreBikeVar2')]),
                              dict(name='MountSpeederBikeOni', category='Ridehover',
                                   displays=[dict(hots_id='Hover'), dict(hots_id='NotRegistered')]),
                              dict(name='Vulture', category='Ridefly', displays=[dict(hots_id='VultureBaseVar1')]),
                              dict(name='CountArthasMorphForm', category='Ridefly', displays=[dict(hots_id='CountArthasMorphForm')]),
                              dict(name='MountWolfDark18', category='Ridebeast', displays=[dict(hots_id='Wolf')])])
        ids = dict(SpectreBikeVar2=dict(display=7, name='Amber Spectre Phantom'),
                   Hover=dict(display=9, name='Azure Orochi Hovercycle'),
                   VultureBaseVar1=dict(display=10, name='Dominion Vulture'),
                   CountArthasMorphForm=dict(display=11, name='CountArthasMorphForm'),
                   Wolf=dict(display=8, name='Ancient Dark Nexus Hound'))
        self.assertEqual(pose_rows(report, ids), ['10;r;806;484', '11;h;0;0', '7;r;806;484', '9;r;806;484'])

    def test_seat_shift_moves_only_listed_mounts_back(self):
        text = 'hots/mounts/a.m3;0.5;0;1\nhots/mounts/b.m3;0.5;0;1\n'
        self.assertEqual(shift_seats(text, {'hots/mounts/a.m3'}, 0.25), 'hots/mounts/a.m3;0.25;0;1\nhots/mounts/b.m3;0.5;0;1\n')


if __name__ == '__main__':
    unittest.main()
