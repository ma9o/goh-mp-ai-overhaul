import io
import sys
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from map_sound_folders import Resources, audit


def archive(files):
    data = io.BytesIO()
    with zipfile.ZipFile(data, 'w') as z:
        for name, text in files.items():
            z.writestr(name, text)
    return zipfile.ZipFile(io.BytesIO(data.getvalue()))


def base(**files):
    return {'sound.set': '{sound {loudness default 5 100 0}}',
            'folder.set': '{sound {loudness default}}', **files}


class SoundFoldersTest(unittest.TestCase):
    def run_audit(self, files):
        with archive(files) as z:
            return audit(Resources({'sound/test.pak': z}))

    def test_signal_inherits_through_missing_folder_and_audio_override(self):
        files = base(**{
            'human/folder.set': '{sound {signal movement 8} {SignalPeriod 0.5}}',
            'human/gear/fast/folder.set': '{sound {volume 0} {loudness default}}',
            'human/gear/fast/cloth/a.wav': b'not decoded',
            'human/gear/fast/b.wav': b'not decoded',
        })
        out = self.run_audit(files)
        fast = next(r for r in out['folders'] if r['path'] == 'human/gear/fast')
        self.assertEqual(fast['effective']['signal'], {'type': 'movement', 'radius_input': 8})
        self.assertEqual(fast['effective']['signal_period'], 0.5)
        self.assertEqual(fast['effective']['volume'], 0)
        self.assertEqual(fast['provenance']['signal']['folder'], 'human')
        self.assertEqual([r['immediate_wav_count'] for r in out['asset_directories']], [1, 1])
        self.assertTrue(all(r['effective_declaration'] == 'human/gear/fast'
                            for r in out['asset_directories']))

    def test_nested_macro_include_order_and_no_signal_period_reset(self):
        files = base(**{
            'defines.inc': '(define outer (include "\\sound\\presets.inc") '
                           '(inner) {signal shot 180})',
            'presets.inc': '(define inner {signal shot 160} {SignalPeriod 7})',
            'gun/folder.set': '{sound (include "\\sound\\defines.inc") (outer)}',
        })
        out = self.run_audit(files)
        gun = next(r for r in out['folders'] if r['path'] == 'gun')
        self.assertEqual(gun['effective']['signal']['radius_input'], 180)
        self.assertEqual(gun['effective']['signal_period'], 7)
        self.assertEqual(out['macro_calls'], {'inner': 1, 'outer': 1})
        self.assertEqual([r['updates']['signal']['radius_input'] for r in gun['assignments']
                          if r['property'] == 'signal'], [160, 180])

    def test_loudness_redefinition_and_unknown_fallback(self):
        files = base(**{
            'sound.set': '{sound {loudness default 5 100 0} '
                         '{loudness x 1 9 7} {loudness X 2 8 6}}',
            'a/folder.set': '{sound {loudness x}}',
            'b/folder.set': '{sound {loudness missing}}',
        })
        out = self.run_audit(files)
        folders = {r['path']: r for r in out['folders']}
        self.assertEqual(folders['a']['effective']['min_distance'], {'input': 2})
        self.assertEqual(folders['a']['effective']['priority'], 6)
        self.assertEqual(folders['b']['effective']['max_distance'], {'input': 100})
        self.assertTrue(folders['b']['provenance']['priority']['loudness_fallback'])

    def test_missing_macro_cycle_and_unreviewed_syntax_fail(self):
        cases = [('{sound (missing)}', {}, 'Unresolved'),
                 ('{sound (include "\\sound\\defs.inc") (a)}',
                  {'defs.inc': '(define a (b)) (define b (a))'}, 'Cyclic sound macro'),
                 ('{sound (include "\\sound\\defs.inc")}',
                  {'defs.inc': '(include "defs.inc")'}, 'Cyclic sound include'),
                 ('{sound {signal bogus 4}}', {}, 'Unreviewed signal'),
                 ('{sound {invented 1}}', {}, 'Unreviewed sound-folder')]
        for declaration, extra, error in cases:
            with self.subTest(error=error), self.assertRaisesRegex(ValueError, error):
                self.run_audit(base(**{'x/folder.set': declaration, **extra}))

    def test_ambiguous_mount_and_case_collision_fail(self):
        with archive(base()) as first, archive({'folder.set': '{sound}'}) as second:
            with self.assertRaisesRegex(ValueError, 'Ambiguous mounted'):
                Resources({'a.pak': first, 'b.pak': second})
        with archive(base(**{'FOLDER.SET': '{sound}'})) as z:
            with self.assertRaisesRegex(ValueError, 'Case-colliding'):
                Resources({'a.pak': z})


if __name__ == '__main__':
    unittest.main()
