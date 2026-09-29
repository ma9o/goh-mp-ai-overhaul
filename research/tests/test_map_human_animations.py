import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from map_human_animations import Reader, compile_catalog, parse


class AnimationDeclarationsTest(unittest.TestCase):
    def catalog(self, text):
        return compile_catalog(parse(text, 'test.ext'))

    def test_paths_and_nested_events(self):
        result = self.catalog(r'''{animation {smooth 0.5}
            {sequence "walk" {file "locomotion\run.anm"}
              {events {11 "leg_right"} {31 "leg_left"}}
              {dynamicSpeed {10 20 0.6}}}}''')
        row = result['definitions'][0]
        self.assertEqual(row['properties'][0]['values'], [r'locomotion\run.anm'])
        self.assertEqual([(e['frame'], e['name']) for e in row['events']],
                         [(11, 'leg_right'), (31, 'leg_left')])
        self.assertEqual(row['dynamic_speed_declarations'][0]['factor'], 0.6)

    def test_include_order_and_default_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'properties.pak'
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('human/root.ext', '{animation {smooth 0.5}'
                                 '(include "a.inc"){sequence "after"}}')
                archive.writestr('human/a.inc', '{sequence "before" {smooth 0.1}}'
                                 '{smooth 0.2}{blend "pose" {pose "walk" end}}')
            with zipfile.ZipFile(path) as archive:
                reader = Reader(archive)
                rows = compile_catalog(reader.read('human/root.ext'))['definitions']
            self.assertEqual([r['inherited_smooth'] for r in rows], [0.5, 0.2, 0.2])
            self.assertEqual(rows[0]['member'], 'human/a.inc')
            self.assertEqual(rows[2]['member'], 'human/root.ext')

    def test_duplicate_declarations_are_preserved(self):
        result = self.catalog('{animation {sequence "A"}{sequence "a"}}')
        self.assertEqual(result['duplicate_names_casefolded'], {'a': 2})
        self.assertEqual(len(result['definitions']), 2)

    def test_unknown_properties_fail(self):
        with self.assertRaisesRegex(ValueError, 'Unreviewed'):
            self.catalog('{animation {sequence "a" {unknown 1}}}')

    def test_malformed_delimiters_fail(self):
        with self.assertRaises(ValueError):
            self.catalog('{animation (include "a"}}')

    def test_include_cycle_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test.pak'
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('a', '(include "b")')
                archive.writestr('b', '(include "a")')
            with zipfile.ZipFile(path) as archive:
                with self.assertRaisesRegex(ValueError, 'Cyclic'):
                    Reader(archive).read('a')


if __name__ == '__main__':
    unittest.main()
