import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from map_marker_actions import extract


class MarkerActionsTest(unittest.TestCase):
    def test_closure_cycles_and_unexpanded_macros(self):
        text = '''{"human" (define "m" {start_sound "human/%0"})
          {on animation_event "step" ("m" args normal) {call "a"}}
          {on "a" {call "b"}} {on "b" {call "a"}}
          {on "unrelated" {delete}}}'''
        catalog = {'definitions': [{'events': [{'name': 'step'}, {'name': 'absent'}]}]}
        out = extract(text, 'human.inc', catalog)
        self.assertEqual(out['counts']['reached_handlers_and_macros'], 4)
        self.assertNotIn('delete', out['counts']['action_node_names'])
        self.assertEqual(out['macro_invocations_unexpanded'][0]['arguments'], ['args', 'normal'])
        self.assertEqual(out['unresolved_dependencies'], [])
        self.assertEqual(out['marker_join'][0]['human_handler_count'], 0)

    def test_comments_missing_calls_and_sound_links(self):
        text = '''{"human" {on spawn {link_sound "x" "sound/x"}}
          ; {on animation_event "commented" {delete}}
          {on animation_event "x" {play_sound "x"} {call_synced "missing"}}}'''
        out = extract(text, 'human.inc', {'definitions': []})
        self.assertEqual(out['counts']['marker_handlers'], 1)
        self.assertEqual(out['unresolved_dependencies'][0]['to_name'], 'missing')
        self.assertEqual(out['spawn_sound_links'][0]['items'], ['link_sound', 'x', 'sound/x'])

    def test_ambiguous_event_fails(self):
        with self.assertRaisesRegex(ValueError, 'Ambiguous'):
            extract('{"human" {on "a"} {on "a"}}', 'human.inc', {'definitions': []})


if __name__ == '__main__':
    unittest.main()
