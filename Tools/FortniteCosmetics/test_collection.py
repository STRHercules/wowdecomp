import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

from PIL import Image

from stage_collection import collection_sql
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'WolfPup'))
from build_model import build


class CollectionTests(unittest.TestCase):
    def test_scope_keeps_existing_weapon_and_allocates_distinct_cloak_learning_items(self):
        ids={'cat':dict(source=3400101,token=3410101,display=3400101)}
        sql,items=collection_sql([dict(slug='cat',name="Cat's Pet")],
            [dict(display=3430001,existing=True),dict(display=3430002,name='New Axe')],ids)
        self.assertEqual(items,[3400101,3410101,3430002])
        self.assertNotIn('3430001',sql)
        self.assertIn("'Cat''s Pet'",sql)
        self.assertIn("(3430002,2,1,'New Axe',3430002,4,17",sql)
        self.assertIn('(3400101,3410101)',sql)
        self.assertIn('item_esteria_backpack_appearance',sql)

    def test_animated_encoder_preserves_material_slots_and_palette_skinning(self):
        with tempfile.TemporaryDirectory() as temporary:
            out=Path(temporary);texture=out/'source.png';Image.new('RGB',(4,4),'white').save(texture)
            vertices=[[0,0,0],[1,0,0],[0,1,0],[0,0,1],[1,0,1],[0,1,1]]
            mesh=dict(name='pet',vertices=vertices,normals=[[0,0,1]]*6,uvs=[[0,0]]*6,
                      weights=[[[0,1]]]*6,triangles=[[0,1,2],[3,4,5]],triangle_materials=[0,1])
            frame=[[[0,0,0],[0,0,0,1],[1,1,1]]]
            moved=[[[.01,0,0],[0,0,0,1],[1,1,1]]]
            baked=dict(meshes=[mesh],bones=[dict(name='root',pivot=[0,0,0])],facial_helper_count=0,
                       clips=[dict(name='Idle',frames=2,fps=30,transforms=[frame,moved],
                                   animated_shape_keys=[],sample_max_position_error=0)],
                       asset=dict(slug='test',name='Test',model='custom/fortnite/pets/test.m2',
                                  texture_paths=['custom/fortnite/pets/a.blp','custom/fortnite/pets/b.blp'],textures=[str(texture)]*2))
            (out/'baked.json').write_text(json.dumps(baked));build(out)
            model=(out/'patch-root/custom/fortnite/pets/test.m2').read_bytes()
            skin=(out/'patch-root/custom/fortnite/pets/test00.skin').read_bytes()
            self.assertEqual(struct.unpack_from('<I',model,80)[0],2)
            count,offset=struct.unpack_from('<II',skin,36)
            self.assertEqual(count,2)
            self.assertEqual([struct.unpack_from('<BB11H',skin,offset+i*24)[6] for i in range(2)],[0,1])
            self.assertEqual(json.loads((out/'catalog.json').read_text())['model'],baked['asset']['model'])

if __name__=='__main__':unittest.main()
