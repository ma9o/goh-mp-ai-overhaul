"""Small hand-authored cases that distinguish the reviewed graph semantics."""
import io
import sys
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from map_human_fsm import Expander, compile_graph, enabled_states, parse, route


def graph(text):
    return compile_graph(parse(text, 'fixture'))


class FsmTests(unittest.TestCase):
    def test_comments_do_not_eat_quoted_semicolon(self):
        nodes = parse('{State "a;b"} ; {ignored}\n', 'fixture')
        self.assertEqual(nodes[0].items, ['State', 'a;b'])
        for invalid in ('{State "bad}', '{State "x")', '{State "a\\nb"}'):
            with self.assertRaises(ValueError):
                parse(invalid, 'fixture')

    def test_macro_include_preserves_statement_order(self):
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, 'w') as archive:
            archive.writestr('properties/human_fsm.ext',
                             '{FSM "human" (include "tiny.inc")}')
            archive.writestr('properties/tiny.inc', '''
                {Split 2} {Start "s"}
                (define "make" {State "%0" {tag "up"}})
                ("make" args s)
                {State "target" {fromTagged "up"} {tag "go"}}
                ("make" args later)
            ''')
        buffer.seek(0)
        with zipfile.ZipFile(buffer) as archive:
            # In-memory ZIPs do not have a filename; the production path does.
            archive.filename = 'fixture.pak'
            expander = Expander(archive)
            g = compile_graph(expander.read('properties/human_fsm.ext'))
        states = {s['name']: s for s in g['states']}
        self.assertEqual(states['s']['neighbors'], ['target'])
        self.assertEqual(states['later']['neighbors'], [])
        self.assertEqual(states['s']['parts'], ['linked', 'linked'])
        self.assertEqual(expander.invocations, 2)

    def test_reverse_neighbor_order_breaks_equal_length_tie(self):
        g = graph('''{FSM "human" {Start "s"}
            {State "s" {to "a"} {to "b"}}
            {State "a" {to "far"} {tag "go"}}
            {State "b" {tag "go"}}
            {State "far" {tag "go"}}
        }''')
        self.assertEqual(route(g, 's', 'go', enabled_states(g, None)), ['b'])

    def test_subset_last_match_wins_and_disabled_source_can_satisfy(self):
        g = graph('''{FSM "human" {Start "s"}
            {State "s" {tag "up"} {tag "go"} {to "t"}}
            {State "t" {tag "up"} {tag "item"} {tag "other"}}
            {Subset "default" {disable "up"}}
            {Subset "item" from "default" {enable "item"}}
        }''')
        enabled = enabled_states(g, 'item')
        self.assertEqual(enabled, {'s': False, 't': True})
        self.assertEqual(route(g, 's', 'go', enabled), [])
        self.assertEqual(route(g, 's', 'other', enabled), ['t'])

    def test_conditions_filter_entry_and_mask_is_all_of(self):
        g = graph('''{FSM "human" {Start "s"}
            {State "s" {to "gate"} {tag "one"}}
            {State "gate" {condition "allowed"} {to "t"}}
            {State "t" {tag "wanted"}}
            {Subset "both" {disable "one" "two"}}
        }''')
        enabled = enabled_states(g, 'both')
        self.assertTrue(enabled['s'])
        self.assertIsNone(route(g, 's', 'wanted', enabled))

    def test_switch_pairs_match_other_parts_and_keep_explicit_links(self):
        g = graph('''{FSM "human" {Split 2} {Start "a"}
            {Request "move" {priority 30} {duration 2} {switch "walk" 1}}
            {State "a" {split "stand" "aim"}}
            {State "b" {split "walk" "aim"}}
            {State "c" {split "walk" "other"}}
            {Switch "walk" {split 0 "stand" "walk"} {link "a" "c"}}
        }''')
        self.assertEqual(g['switches'][0]['pairs'], [['a', 'b'], ['a', 'c']])
        self.assertEqual(g['requests'][0]['duration'], 2)
        self.assertFalse(g['requests'][0]['reverse'])

    def test_unknown_directive_and_undeclared_reference_fail(self):
        for text in ('{FSM "human" {Unknown "x"}}',
                     '{FSM "human" {Start "s"} {State "s" {to "missing"}}}'):
            with self.assertRaises(ValueError):
                graph(text)


if __name__ == '__main__':
    unittest.main()
