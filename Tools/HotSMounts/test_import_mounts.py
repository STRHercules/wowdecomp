import json
import tempfile
import unittest
from pathlib import Path

from import_mounts import inventory, virtual
from catalog import audit_catalog


class ImportTests(unittest.TestCase):
    def test_roster_and_named_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            model = dict(name='Test', source='mount/test.m3', category='Ridesurf', rider=[0, 0, 1],
                         bounds={}, animations=[], displays=[dict(hots_id='TestBlue', textures=['blue', 'missing', ''])])
            catalog = root / 'mounts.json'
            catalog.write_text(json.dumps([model]))
            index = root / 'index.txt'
            index.write_text('mods/heroes.stormmod/base.stormassets/assets/units/mount/test.m3\n'
                             'mods/heroes.stormmod/base.stormassets/assets/units/mount/test_requiredanims.m3a\n'
                             'mods/heroes.stormmod/base.stormassets/assets/textures/blue.dds\n'
                             'mods/heroesdata.stormmod/base.stormdata/gamedata/mounts/test.xml\n')
            report, resolve = inventory(catalog, index)
            self.assertEqual(report['counts']['resolvedModels'], 1)
            self.assertEqual(report['counts']['variants'], 1)
            self.assertEqual(len(report['requestedFiles']), 4)
            self.assertEqual(len(report['models'][0]['banks']), 1)
            self.assertIsNone(report['models'][0]['displays'][0]['nativeTextures'][1]['source'])
            self.assertFalse(report['completeRosterVerified'])
            self.assertIsNone(resolve('missing.dds'))
            catalog.write_text(json.dumps([model, model]))
            with self.assertRaisesRegex(ValueError, 'Duplicate'):
                inventory(catalog, index)

    def test_unsafe_paths(self):
        for path in ('../escape.m3', 'assets/../escape.dds', 'C:/escape.dds', '/root.m3', 'bad;path', 'bad\npath'):
            with self.assertRaises(ValueError):
                virtual(path)

    def test_inherited_mount_and_model_catalog(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'mount.xml').write_text('<Catalog>'
                '<CModel id="Base"><Model value="assets/mount.m3"/><RequiredAnims value="assets/body.m3a"/></CModel>'
                '<CModel id="BlueModel" parent="Base"/>'
                '<CMount id="Template" default="1"><Model value="BlueModel"/><MountCategory value="Ride"/></CMount>'
                '<CMount id="Blue" parent="Template"/><CMount id="Red" parent="Template"/>'
                '</Catalog>')
            report = audit_catalog(['mount.xml'], root, {'Blue'}, lambda p: 'resolved/' + p)
            self.assertEqual(report['concreteMounts'], 2)
            self.assertEqual(report['additionalSourceIds'], ['Red'])
            self.assertEqual(report['mounts'][0]['source'], 'resolved/assets/mount.m3')
            self.assertEqual(report['mounts'][0]['requiredAnims'], 'resolved/assets/body.m3a')
            self.assertEqual(report['mounts'][0]['category'], 'Ride')


if __name__ == '__main__':
    unittest.main()
