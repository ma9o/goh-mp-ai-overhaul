import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from map_idle_policy import extract


class IdlePolicyTest(unittest.TestCase):
    def test_empty_branch_preserved_before_else(self):
        result = extract('{on "select_idle" {if in_cover else standing '
                         '{set_idle_animation "a"} else {set_idle_animation "b"}}}', 'test')
        branches = result['actions'][0]['branches']
        self.assertEqual([b['condition'] for b in branches], ['in_cover', 'standing', None])
        self.assertEqual(branches[0]['actions'], [])

    def test_named_macro_substitution(self):
        result = extract('''{on "select_idle"
            (define "pick" {if rand %p {set_idle_animation %name}})
            ("pick" p(0.5) name("a")) ("pick" p(1) name("b"))}''', 'test')
        self.assertEqual(result['animation_names'], ['a', 'b'])
        self.assertEqual(result['actions'][0]['branches'][0]['arguments'], ['0.5'])
        self.assertEqual(len(result['macro_invocations']), 2)

    def test_negation_and_multiple_descriptors(self):
        result = extract('{on "select_idle" {if not stuff "mgun weapon" "pistol weapon" '
                         '{set_idle_animation "a"}}}', 'test')
        branch = result['actions'][0]['branches'][0]
        self.assertTrue(branch['negated'])
        self.assertEqual(branch['arguments'], ['mgun weapon', 'pistol weapon'])

    def test_missing_argument_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Missing macro argument'):
            extract('{on "select_idle" (define "p" {set_idle_animation %name})("p")}', 'test')

    def test_unknown_condition_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Unsupported condition'):
            extract('{on "select_idle" {if unknown}}', 'test')


if __name__ == '__main__':
    unittest.main()
