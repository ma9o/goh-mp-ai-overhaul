import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from map_animation_resources import join_declarations, parse_anm, runs, scan_archive


def pack(fmt, *values):
    return struct.pack('<' + fmt, *values)


def header(names=('root',), duration=11, version=0x60000):
    return (b'EANM' + pack('i', version) + b'FRMS' + pack('i', duration)
            + b'BMAP' + pack('i', len(names))
            + b''.join(pack('i', len(n)) + n.encode() for n in names))


def frm2(coord, updates):
    return b'FRM2' + pack('HB', coord, len(updates)) + b''.join(
        pack('BH', index, flags) + data for index, flags, data in updates)


IDENTITY = pack('12f', 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)


class BinaryAnimationTest(unittest.TestCase):
    def test_sparse_coordinates_and_pose_binding_order(self):
        data = header(('placement', 'arm'))
        data += frm2(0, [(1, 3, pack('6f', *([0] * 6))),
                         (0, 1, pack('3f', 0, 0, 0))])
        data += frm2(10, [(1, 2, pack('3f', 0, 0, 0))])
        r = parse_anm(data)
        self.assertEqual(r['frame_coordinate_runs'], [[0, 0], [10, 10]])
        self.assertEqual(r['first_frame_pose_order'], [1, 0])
        self.assertEqual([b['track_index'] for b in r['bones']], [1, 0])
        self.assertEqual(r['pose_track_count'], 2)
        self.assertEqual(r['counts']['pose_updates'], 3)

    def test_visibility_mesh_combines_one_action(self):
        r = parse_anm(header(('arm_lod3',)) + frm2(0, [
            (0, 0x38, pack('i', 3) + b'abc'), (0, 0x10, b'')]))
        self.assertEqual(r['counts']['frame_actions'], 2)
        self.assertEqual(r['counts']['mesh_payload_bytes'], 3)
        self.assertEqual(r['counts']['show_updates'], 1)
        self.assertEqual(r['counts']['hide_updates'], 1)
        self.assertEqual(r['bones'][0]['lod'], 3)
        self.assertEqual(r['pose_track_count'], 0)

    def test_frmn_matrix_visibility_and_mesh(self):
        data = header() + b'FRMN' + pack('i', 0) + b'BONE' + pack('i', 0)
        data += b'MATR' + IDENTITY + b'VISI' + pack('i', -1) + b'MESH' + pack('i', 0)
        data += b'FRMN' + pack('i', 10)
        r = parse_anm(data)
        self.assertEqual(r['counts']['frame_actions'], 1)
        self.assertEqual(r['counts']['FRMN'], 2)
        self.assertEqual(r['counts']['matrix_updates'], 1)
        self.assertEqual(r['bones'][0]['mirror'], 0)

    def test_reflected_matrix(self):
        reflected = pack('12f', 1, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0)
        r = parse_anm(header() + b'FRMN' + pack('i', 0) + b'BONE'
                      + pack('i', 0) + b'MATR' + reflected)
        self.assertEqual(r['bones'][0]['mirror'], 1)

    def test_late_track_and_duplicate_first_track_are_reported(self):
        update = (0, 1, pack('3f', 0, 0, 0))
        r = parse_anm(header(('a', 'b')) + frm2(0, [update, update])
                      + frm2(1, [(1, 1, update[2])]))
        self.assertEqual(r['anomalies']['duplicate_first_frame_pose_bones'], [0])
        self.assertEqual(r['anomalies']['late_unbound_pose_bones'], [1])
        self.assertEqual(r['pose_track_count'], 2)

    def test_unknown_flag_and_nonfinite_values_are_reported(self):
        r = parse_anm(header() + frm2(0, [(0, 0x8001, pack('3f', float('inf'), 0, 0))]))
        self.assertEqual(r['anomalies']['unknown_flags'], [0x8000])
        self.assertEqual(r['anomalies']['nonfinite_payload_values'], 1)

    def test_empty_truncated_negative_size_and_bad_indices_rejected(self):
        cases = [header(), header() + b'FRM2',
                 header() + frm2(0, [(0, 0x20, pack('i', -1))]),
                 header() + frm2(0, [(1, 0, b'')]),
                 header() + frm2(11, []), header() + b'NOPE',
                 header(version=0x20000), header(duration=0)]
        for data in cases:
            with self.subTest(data=data[-12:]), self.assertRaises(ValueError):
                parse_anm(data)

    def test_coordinate_order_is_preserved(self):
        r = parse_anm(header() + frm2(3, []) + frm2(2, []) + frm2(2, []))
        self.assertFalse(r['strictly_increasing_frames'])
        self.assertEqual(r['frame_coordinate_runs'], [[3, 3], [2, 2], [2, 2]])
        self.assertEqual(runs([0, 1, 2, 4, 5]), [[0, 2], [4, 5]])

    def test_directory_join_is_immediate_and_markers_use_stored_frames(self):
        catalog = {'definitions': [{'index': 0, 'name': 'walk', 'type': 'sequence',
                    'properties': [{'name': 'file', 'values': ['DIR']}],
                    'events': [{'frame': 5, 'name': 'step'}, {'frame': 10, 'name': 'end'}]}]}
        records = [{'member': name, 'declared_frame_count': 11,
                    'frame_coordinate_runs': [[0, 0], [10, 10]]}
                   for name in ('human/dir/a.anm', 'human/dir/sub/b.anm')]
        row = join_declarations(catalog, records, 'human')[0]
        self.assertEqual(row['resource_indices'], [0])
        self.assertEqual(row['marker_checks'][0]['in_bounds_resource_indices'], [0])
        self.assertEqual(row['marker_checks'][0]['stored_in_resource_indices'], [])
        self.assertEqual(row['marker_checks'][1]['stored_in_resource_indices'], [0])

    def test_extension_missing_and_rejected_resources(self):
        catalog = {'definitions': [{'index': i, 'name': name, 'type': 'sequence',
                    'properties': [], 'events': [{'frame': 0, 'name': 'x'}]}
                   for i, name in enumerate(('A.ANM', 'absent'))]}
        records = [{'member': 'human/a.anm', 'parse_status': 'rejected'}]
        rows = join_declarations(catalog, records, 'human')
        self.assertEqual(rows[0]['resource_indices'], [0])
        self.assertEqual(rows[0]['parsed_resource_indices'], [])
        self.assertEqual(rows[1]['resource_indices'], [])

    def test_archive_audit_preserves_rejection_and_empty_directory(self):
        catalog = {'definitions': [{'index': i, 'name': name, 'type': 'sequence',
                    'properties': [], 'events': [{'frame': 1, 'name': 'x'}]}
                   for i, name in enumerate(('a', 'folder', 'bad'))]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test.pak'
            with zipfile.ZipFile(path, 'w') as z:
                z.writestr('human/a.anm', header() + frm2(0, []) + frm2(10, []))
                z.writestr('human/bad.anm', header())
                z.writestr('human/folder/deeper/a.anm', header() + frm2(0, []))
            with zipfile.ZipFile(path) as z:
                out = scan_archive(z, 'human', catalog)
        self.assertEqual(out['counts']['parsed'], 2)
        self.assertEqual(out['counts']['rejected'], 1)
        self.assertEqual(out['declaration_join'][1]['archive_join_mode'], 'directory')
        self.assertEqual(out['declaration_join'][1]['resource_indices'], [])
        self.assertEqual(out['audit']['marker_coordinate_mismatches'][0]['frame'], 1)


if __name__ == '__main__':
    unittest.main()
