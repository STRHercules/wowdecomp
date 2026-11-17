import unittest
import struct
import numpy as np
from PIL import Image
from elemental_swords import effect_frame,sample_wrap,rgba_blp,orient,TILE

class ElementalTests(unittest.TestCase):
    def test_fire_and_flow_loops_move_and_return_seamlessly(self):
        rng=np.random.default_rng(19);noise=rng.random((TILE,TILE,3))
        for kind,inputs in [('fire',dict(noise=noise,mask=noise)),('plasma',dict(flow=noise,mask=noise,emissive=np.ones_like(noise)*[.1,.5,.9]))]:
            a=effect_frame(kind,0,inputs);b=effect_frame(kind,.3,inputs);end=effect_frame(kind,1,inputs)
            self.assertGreater(np.abs(a.astype(float)-b).mean(),1)
            np.testing.assert_allclose(a,end,atol=1)
    def test_bilinear_sampler_wraps_negative_coordinates(self):
        image=np.asarray([[[1.,0,0],[0,1,0]]])
        np.testing.assert_allclose(sample_wrap(image,np.asarray([-.25]),np.asarray([0])),[[.5,.5,0]])
    def test_blp_keeps_transparency_and_first_mip_payload(self):
        image=Image.new('RGBA',(4,4),(12,34,56,78));blob=rgba_blp(image)
        self.assertEqual(blob[:4],b'BLP2');offset=struct.unpack_from('<I',blob,20)[0]
        self.assertEqual(blob[offset:offset+4],bytes([56,34,12,78]))
    def test_source_axes_are_rotated_without_recentering_the_grip(self):
        mesh=dict(vertices=[(0,0,0),(0,0,1)],normals=[(0,0,1)]*2)
        self.assertEqual(orient(mesh,'fire')['vertices'],[(0,0,0),(1,0,0)])
        mesh=dict(vertices=[(0,0,0),(0,-1,0)],normals=[(0,-1,0)]*2)
        self.assertEqual(orient(mesh,'plasma')['vertices'],[(0,0,0),(1,0,0)])

if __name__=='__main__':unittest.main()
