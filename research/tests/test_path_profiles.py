import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from map_path_profiles import obstacles, profiles


class PathProfileAuditTests(unittest.TestCase):
    def test_nested_macros_keep_order_and_terrain_permission(self):
        text = '''{Pathers
          (define "base" {Network} {Human} {Terrain "ground" %k 0 "water" 0})
          (define "make" {Profile "%name" ("base" k(%k)) {Radius 0.3}})
          ("make" name(human) k(0.25))
          {Profile "other" {LinearPath}}
        }'''
        rows = profiles(text, 'fixture')['profiles']
        self.assertEqual([p['name'] for p in rows], ['human', 'other'])
        self.assertEqual(rows[0]['flags'], 5)
        self.assertEqual(rows[0]['height_costs'], [0.0] * 15)
        ground, water = rows[0]['terrain_declarations']
        self.assertEqual(ground['cost_model'], 1)
        self.assertFalse(ground['permission'])
        self.assertEqual(water['cost_model'], -1)
        self.assertTrue(water['permission'])
        self.assertEqual(rows[0]['terrain_costs_model'][0], -1)
        self.assertEqual(rows[0]['terrain_costs_model'][1], 1)
        self.assertEqual(rows[0]['terrain_costs_model'][7], 0)
        self.assertFalse(rows[0]['terrain_permissions'][1])

    def test_negative_obstacle_cost_preserves_previous_value(self):
        rows = [{'name': 'human'}, {'name': 'vehicle'}, {'name': 'aircraft'}]
        text = '''{obstacles {wall {flag_vaultable}
          {human 5} {human -1} {vehicle -1}}}'''
        result = obstacles(text, 'fixture', rows)['obstacles'][0]
        self.assertEqual(result['costs'], {'human': 5.0, 'vehicle': 100000.0,
                                         'aircraft': 100000.0})
        self.assertEqual(result['flags'], 16)

    def test_unreviewed_or_malformed_inputs_fail(self):
        for text in ('{Pathers {Profile "x" {Unknown}}}',
                     '{Pathers {Profile "x" {Height 0 0}}}',
                     '{Pathers {Profile "x"} {Profile "x"}}',
                     '{Pathers ("missing")}',
                     '{Pathers {Profile "x" {Terrain "ground" nan}}}'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                profiles(text, 'fixture')
        with self.assertRaises(ValueError):
            obstacles('{obstacles {wall {unknown 2}}}', 'fixture', [{'name': 'human'}])


if __name__ == '__main__':
    unittest.main()
