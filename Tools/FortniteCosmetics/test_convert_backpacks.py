import io
import struct
import unittest
from pathlib import Path
import tempfile

from PIL import Image

import convert_backpacks as convert


class ConversionTests(unittest.TestCase):
    def mesh(self):
        return {'source_name':'TwoMaterials','vertices':[(0,0,0),(0,1,0),(0,0,1),(0,-1,0)],
                'normals':[(1,0,0)]*4,'uvs':[(0,0),(1,0),(0,1),(1,1)],'indices':[0,1,2,0,2,3],
                'materials':[{'first_index':0,'faces':1},{'first_index':3,'faces':1}]}

    def test_material_ranges_and_opacity(self):
        model,skin,dimensions=convert.write_model(self.mesh(),['custom/a.blp','custom/b.blp'])
        convert.verify(model,skin)
        self.assertEqual(dimensions,[0,2,1])
        count,offset=struct.unpack_from('<II',skin,36)
        self.assertEqual(count,2)
        second=struct.unpack_from('<BB11H',skin,offset+24)
        self.assertEqual((second[3],second[6],second[8],second[9]),(1,1,1,1))

    def test_bad_index_and_track_fail(self):
        model,skin,_=convert.write_model(self.mesh(),['a.blp','b.blp'])
        bad=bytearray(skin)
        _,offset=struct.unpack_from('<II',bad,12)
        struct.pack_into('<H',bad,offset,99)
        with self.assertRaises(AssertionError): convert.verify(model,bad)
        bad=bytearray(model)
        _,offset=struct.unpack_from('<II',bad,92-4)
        struct.pack_into('<II',bad,offset+4,1,len(bad)+16)
        with self.assertRaises(ValueError): convert.verify(bad,skin)

    def test_source_truncation_is_rejected(self):
        with self.assertRaises(ValueError): convert.read_mesh(b'UEFORMAT')
        with self.assertRaises(ValueError): convert.Reader(struct.pack('<i',-1)).count()

    def test_source_texture_and_default_variant(self):
        material={'name':'M_MED_Rays_Backpack'}
        base=Path('T_F_MED_Rays_Backpack_D.png')
        silver=Path('T_F_MED_Rays_Backpack_Silver_D.png')
        self.assertEqual(convert.pick_diffuse([base,silver],material),base)
        self.assertIsNone(convert.pick_diffuse([Path('abc_D.png'),Path('xyz_D.png')],material))

    def test_bc1_texture_roundtrip(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'texture.png'
            Image.new('RGBA',(8,4),(50,100,150,10)).save(path)
            texture=convert.texture_bytes(path)
            decoded=Image.open(io.BytesIO(texture))
            decoded.load()
            self.assertEqual(decoded.size,(8,4))
            rgb=decoded.convert('RGB').getpixel((2,2))
            self.assertTrue(all(abs(a-b)<=8 for a,b in zip(rgb,(50,100,150))))
            self.assertEqual(len(texture)-1172,16+8+8+8)


if __name__=='__main__': unittest.main()
