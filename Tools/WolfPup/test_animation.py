import unittest
from build_model import continuous_quaternions,playback_clip,sample_quaternions
from repair_normals import normals
import numpy as np

class AnimationTests(unittest.TestCase):
    def test_decomposed_rotations_are_normalized_before_interpolation(self):
        self.assertEqual(continuous_quaternions([[0,0,0,.9998],[0,0,0,-1.0002]]),[[0,0,0,1],[0,0,0,1]])
        with self.assertRaises(ValueError):continuous_quaternions([[0,0,0,0]])
    def test_opposing_faces_keep_source_normals_when_face_sums_cancel(self):
        vertices=np.asarray([[0.,0.,0.],[1.,0.,0.],[0.,1.,0.]])
        result=normals(vertices,np.asarray([[0,1,2],[2,1,0]]),np.asarray([[0.,0.,1.]]*3))
        np.testing.assert_allclose(result,[[0,0,1]]*3)
    def test_fast_joint_twists_are_subdivided_without_losing_authored_keys(self):
        times,q=sample_quaternions([0,33],[[0,0,0,1],[0,0,1,0]])
        self.assertEqual((times[0],times[-1]),(0,33))
        self.assertTrue(all(b>a for a,b in zip(times,times[1:])))
        self.assertTrue(all(sum(x*y for x,y in zip(a,b))>0.99 for a,b in zip(q,q[1:])))
        self.assertEqual(q[0],[0,0,0,1]);self.assertAlmostEqual(q[-1][2],1)
    def test_equivalent_rotation_keys_do_not_cross_zero(self):
        source=[[0,0,1,0],[0,0,-1,0],[0,0,1,0]]
        fixed=continuous_quaternions(source)
        self.assertEqual(fixed,[[0,0,1,0]]*3)
        middle=[(a+b)*0.5 for a,b in zip(fixed[0],fixed[1])]
        self.assertAlmostEqual(sum(v*v for v in middle),1)

    def test_full_idle_contains_both_clips_and_returns_to_the_start_pose(self):
        a=[[[0,0,0],[0,0,0,1],[1,1,1]]]
        b=[[[0.1,0,0],[0,0,0,-1],[1,1,1]]]
        result=playback_clip([dict(name='idle',frames=2,fps=30,transforms=[a,b]),dict(name='jaw',frames=2,fps=30,transforms=[b,a])])
        self.assertEqual([s['name'] for s in result['sources']],['idle','jaw'])
        self.assertTrue(all(y>x for x,y in zip(result['times'],result['times'][1:])))
        self.assertEqual(result['transforms'][0][0][0],result['transforms'][-1][0][0])
        for record in result['sources']:
            self.assertEqual(record['frames'],2)

if __name__=='__main__':unittest.main()
