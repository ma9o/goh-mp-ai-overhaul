import io
import sys
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from map_marker_actions import extract
from map_marker_sound_paths import compile_requests, SoundIndex
from map_sound_folders import Resources, audit


class FakeIndex:
    def lookup(self, path):
        return {'path': path, 'status': 'assets_found'}


def requests(text):
    return compile_requests(extract(text, 'human.inc', {'definitions': []}), FakeIndex())


class MarkerSoundPathsTest(unittest.TestCase):
    def test_unused_forwarded_argument_is_not_invented(self):
        out = requests('''{human
          (define outer (inner args normal %0))
          (define inner {start_sound "human/move/%0" 1})
          {on animation_event step (outer)}}''')
        self.assertEqual(out['requests'][0]['targets'][0]['path'], 'human/move/normal')
        self.assertEqual(out['macro_invocations'][0]['used_arguments'], {})
        self.assertEqual(out['macro_invocations'][1]['unused_argument_indices'], [1])
        with self.assertRaisesRegex(ValueError, 'no binding'):
            requests('''{human (define m {start_sound "human/%0"})
                         {on animation_event step (m)}}''')

    def test_first_match_guards_calls_and_sequential_sibling(self):
        out = requests('''{human {on animation_event step
          {if velocity 16 {start_sound fast} else lying {call crawl}
           else {start_sound normal}} {start_sound after}}
          {on crawl {if not rain {start_sound dry} else {start_sound wet}}}}''')
        rows = {r['arguments'][0]: r for r in out['requests']}
        self.assertEqual(rows['normal']['guards'], [
            {'tokens': ['velocity', '16'], 'result': False},
            {'tokens': ['lying'], 'result': False}])
        self.assertEqual(rows['wet']['guards'][-1], {'tokens': ['not', 'rain'], 'result': False})
        self.assertEqual(rows['wet']['named_calls'][0]['event'], 'crawl')
        self.assertEqual(rows['after']['guards'], [])

    def test_loop_flag_alias_candidates_and_missing_stop_binding(self):
        out = requests('''{human {on spawn {link_sound breath human/breath}}
          {on animation_event step {play_sound breath 1} {start_sound human/step 1}
          {link_sound piano world/piano} {play_sound piano} {stop_sound bagpipes}}}''')
        self.assertTrue(out['requests'][0]['loop'])
        self.assertFalse(out['requests'][1]['loop'])
        self.assertEqual(out['requests'][2]['targets'][0]['origin'], 'marker_closure_declaration')
        self.assertEqual(out['aliases_without_closure_binding'], ['bagpipes'])
        with self.assertRaisesRegex(ValueError, 'Cyclic named'):
            requests('''{human {on animation_event step {call a}}
              {on a {call b}} {on b {call a}}}''')

    def test_asset_lookup_is_immediate_and_checks_inventory(self):
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, 'w') as z:
            for name, body in {
                'sound.set': '{sound {loudness default 5 100 0}}',
                'folder.set': '{sound}', 'a/child/one.wav': b'one',
                'b/one.wav': b'one', 'b/two.wav': b'two',
            }.items():
                z.writestr(name, body)
        with zipfile.ZipFile(io.BytesIO(buffer.getvalue())) as z:
            resources = Resources({'sound/test.pak': z})
            catalog = audit(resources)
            index = SoundIndex(resources, catalog)
            self.assertEqual(index.lookup('a')['matching_wav_count'], 0)
            self.assertEqual(index.lookup('a/child')['selection_rng_draws'], 0)
            self.assertEqual(index.lookup('b')['selection_rng_draws'], 1)
            self.assertEqual(index.lookup('b/two.wav')['selection_rng_draws'], 0)
            self.assertEqual(index.lookup('b/missing')['matching_wav_count'], 0)
            catalog['asset_directories'][0]['immediate_wav_count'] += 1
            with self.assertRaisesRegex(ValueError, 'metadata changed'):
                SoundIndex(resources, catalog)


if __name__ == '__main__':
    unittest.main()
