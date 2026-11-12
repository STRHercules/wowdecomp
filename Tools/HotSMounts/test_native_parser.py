"""Real-source parser regression: legacy cloth layout, truncation and MADD bounds."""
import json
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


class NativeParserTests(unittest.TestCase):
    def test_declared_mount_banks_use_mesh_hierarchy(self):
        root=Path('build/hots-mounts/stage')
        inventory=json.loads((root/'mount-inventory.json').read_text())
        validation=json.loads(Path('build/hots-mounts/client-playable-v3/validation.json').read_text())
        for name in ('DirewolfMount','MountGoatGuardian21','TigerGoldMount'):
            model=next(m for m in inventory['models'] if m['name']==name)
            proof=next(m for m in validation['models'] if m['model']==name)
            self.assertIsNotNone(proof['bank'])
            run=subprocess.run(['build/retail-assets/bin/Release/ASECM3SceneInspect.exe',
                                str(root/'originals'/model['source']),'-1','0',
                                str(root/'originals'/proof['bank']),'--summary'],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout)
            data=json.loads(run.stdout)
            self.assertTrue(any(i>=0 for i in data['sequenceGroups']))

    def test_original_cloth_and_truncation(self):
        root = Path('build/hots-mounts/stage')
        report = json.loads((root/'mount-inventory.json').read_text())
        model = next(m for m in report['models'] if m['name']=='BattleBeastButcherMount')
        original = root/'originals'/model['source']
        tool = Path('build/retail-assets/bin/Release/ASECM3Inspect.exe')
        run = subprocess.run([str(tool),str(original)],capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stdout)
        data=bytearray(original.read_bytes());index,count=struct.unpack_from('<II',data,4)
        section=next(index+i*16 for i in range(count) if data[index+i*16:index+i*16+4]==b'LCHP')
        struct.pack_into('<I',data,section+8,2)
        with tempfile.TemporaryDirectory() as directory:
            bad=Path(directory)/'truncated.m3';bad.write_bytes(data)
            run=subprocess.run([str(tool),str(bad)],capture_output=True,text=True)
            self.assertNotEqual(run.returncode,0)
            self.assertIn('Truncated/overlapping M3 section PHCL v2',run.stdout)


if __name__=='__main__':
    unittest.main()
