#!/usr/bin/env python3
"""Read EANM metadata using the build-25492057 binary loader contract.

No game execution, mesh decoding, pose interpolation, or mounted-file resolution.
The archive join is explicitly relative to --prefix and ignores engine iterator
ordering. Strict bounds checks are this tool's safeguards, not engine guarantees.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import posixpath
import re
import struct
import zipfile
from collections import Counter
from pathlib import Path


class Reader:
    def __init__(self, data: bytes):
        self.data, self.at = data, 0

    def take(self, size: int) -> bytes:
        if size < 0 or size > len(self.data) - self.at:
            raise ValueError(f'Invalid read of {size} bytes at {self.at:#x}')
        out = self.data[self.at:self.at + size]
        self.at += size
        return out

    def unpack(self, fmt: str):
        return struct.unpack('<' + fmt, self.take(struct.calcsize('<' + fmt)))

    def integer(self, fmt='i') -> int:
        return self.unpack(fmt)[0]

    def expect(self, tag: bytes):
        if self.take(len(tag)) != tag:
            raise ValueError(f'Expected {tag!r} at {self.at - len(tag):#x}')

    def peek(self) -> bytes:
        return self.data[self.at:self.at + 4]


def runs(values: list[int]) -> list[list[int]]:
    """Inclusive runs, preserving order and duplicates; not a sorting operation."""
    out = []
    for value in values:
        if out and out[-1][1] + 1 == value:
            out[-1][1] = value
        else:
            out.append([value, value])
    return out


def bone_name(raw: str) -> tuple[str, int]:
    if '_lod' not in raw:
        return raw, 0
    base, suffix = raw.split('_lod', 1)
    # Native helper passes base0 to a CRT integer parser. Restrict this scanner
    # to the unambiguous decimal subset present in the installed human files.
    if not re.fullmatch(r'[+-]?(?:0|[1-9]\d*)', suffix):
        raise ValueError(f'Unreviewed LOD suffix: {raw!r}')
    return base, int(suffix)


def parse_anm(data: bytes) -> dict:
    r = Reader(data)
    r.expect(b'EANM')
    version = r.integer()
    if version < 0x30000:
        raise ValueError('Unsupported old EANM version')
    r.expect(b'FRMS')
    duration = r.integer()
    if duration <= 0:
        raise ValueError('Nonpositive declared frame count')
    r.expect(b'BMAP')
    count = r.integer()
    if not 0 <= count <= 255:
        raise ValueError('Bone count outside reviewed loader range')
    bones = []
    for _ in range(count):
        raw = r.take(r.integer()).decode('cp1251')
        if '\0' in raw:
            raise ValueError('Embedded NUL in bone token')
        base, lod = bone_name(raw)
        bones.append({'name': base, 'lod': lod, 'track_index': -1, 'mirror': 0})
    frames, track_order = [], []
    counters, flags_seen, action_bones = Counter(), Counter(), Counter()
    anomalies = {'late_unbound_pose_bones': set(), 'duplicate_first_frame_pose_bones': set(),
                 'unknown_flags': set(), 'nonfinite_payload_values': 0}

    def floats(n):
        values = r.unpack('f' * n)
        anomalies['nonfinite_payload_values'] += sum(not math.isfinite(x) for x in values)
        return values

    def checked_bone(index):
        if not 0 <= index < len(bones):
            raise ValueError(f'Invalid bone index {index}')
        return index

    def pose(index, mirror):
        if len(frames) == 1:
            if bones[index]['track_index'] != -1:
                anomalies['duplicate_first_frame_pose_bones'].add(index)
            bones[index]['track_index'] = len(track_order)
            bones[index]['mirror'] = mirror
            track_order.append(index)
        elif bones[index]['track_index'] == -1:
            anomalies['late_unbound_pose_bones'].add(index)
        counters['pose_updates'] += 1

    def mesh():
        size = r.integer()
        r.take(size)
        counters['mesh_updates'] += 1
        counters['mesh_payload_bytes'] += size

    while r.at < len(data):
        tag = r.take(4)
        if tag not in (b'FRMN', b'FRM2'):
            raise ValueError(f'Unreviewed chunk {tag!r} at {r.at - 4:#x}')
        frame = r.integer('i' if tag == b'FRMN' else 'H')
        if not 0 <= frame < duration:
            raise ValueError(f'Frame {frame} outside declared range')
        frames.append(frame)
        counters[tag.decode()] += 1
        if tag == b'FRMN':
            while r.peek() == b'BONE':
                r.take(4)
                index = checked_bone(r.integer())
                has_action = False
                while r.peek() in (b'MATR', b'VISI', b'MESH'):
                    sub = r.take(4)
                    if sub == b'MATR':
                        v = floats(12)
                        cross = (v[1]*v[5]-v[4]*v[2], v[3]*v[2]-v[0]*v[5],
                                 v[0]*v[4]-v[3]*v[1])
                        # Approximate reflection classification; Python arithmetic is
                        # wider than native float. No native quaternion math reproduced.
                        direct = all(abs(cross[i] - v[6+i]) <= .001 for i in range(3))
                        mirror = not direct and all(abs(cross[i] + v[6+i]) <= .001
                                                    for i in range(3))
                        if not direct and not mirror:
                            counters['nonorthogonal_matrices'] += 1
                        pose(index, int(mirror))
                        counters['matrix_updates'] += 1
                    else:
                        if not has_action:
                            counters['frame_actions'] += 1
                            action_bones[index] += 1
                            has_action = True
                        if sub == b'VISI':
                            visible = r.integer() != 0
                            counters['show_updates' if visible else 'hide_updates'] += 1
                        else:
                            mesh()
        else:
            for _ in range(r.integer('B')):
                index = checked_bone(r.integer('B'))
                flags = r.integer('H')
                flags_seen[flags] += 1
                if flags & ~0x3f:
                    anomalies['unknown_flags'].add(flags & ~0x3f)
                if flags & 0x38:
                    counters['frame_actions'] += 1
                    action_bones[index] += 1
                if flags & 0x18:
                    counters['show_updates' if flags & 8 else 'hide_updates'] += 1
                if flags & 1:
                    floats(3)
                    counters['translation_updates'] += 1
                if flags & 2:
                    floats(3)
                    counters['quaternion_updates'] += 1
                if flags & 0x20:
                    mesh()
                if flags & 3:
                    pose(index, (flags >> 2) & 1)
    if not frames:
        raise ValueError('No stored frames')
    anomalies = {k: sorted(v) if isinstance(v, set) else v for k, v in anomalies.items()}
    return {
        'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data),
        'version': hex(version), 'declared_frame_count': duration,
        'stored_frame_count': len(frames), 'frame_coordinate_runs': runs(frames),
        'strictly_increasing_frames': all(a < b for a, b in zip(frames, frames[1:])),
        'first_coordinate': frames[0], 'last_coordinate': frames[-1],
        'bone_count': len(bones), 'pose_track_count': len(track_order),
        'bones': bones, 'first_frame_pose_order': track_order,
        'frame_action_bones': {str(k): v for k, v in sorted(action_bones.items())},
        'counts': dict(sorted(counters.items())),
        'frm2_flags': {hex(k): v for k, v in sorted(flags_seen.items())},
        'anomalies': anomalies,
    }


def normalize(path: str) -> str:
    # Reviewed ASCII path subset only; engine has an additional non-ASCII helper.
    if not path.isascii():
        raise ValueError(f'Non-ASCII path outside reviewed join: {path!r}')
    return posixpath.normpath(path.replace('\\', '/').lower())


def join_declarations(catalog: dict, records: list[dict], prefix: str,
                      directories: set[str] | None = None) -> list[dict]:
    by_name = {}
    for i, row in enumerate(records):
        key = normalize(row['member'])
        if key in by_name:
            raise ValueError(f'Case-normalized archive collision: {key}')
        by_name[key] = i
    if directories is None:
        directories = {posixpath.dirname(name) for name in by_name}
    result = []
    for d in catalog['definitions']:
        props = {p['name']: p['values'] for p in d['properties']}
        if d['type'] == 'sequence':
            expression = props.get('file', [d['name']])[0]
        elif d['type'] == 'blend' and 'pose' in props:
            expression = props['pose'][0] + '.anm'
        else:
            raise ValueError(f'Unsupported declaration join: {d["name"]}')
        path = normalize(posixpath.join(prefix, expression.replace('\\', '/')))
        direct = [i for name, i in by_name.items() if posixpath.dirname(name) == path]
        mode = 'directory' if d['type'] == 'sequence' and path in directories else 'file'
        if mode == 'directory':
            matches = direct
        else:
            if not path.endswith('.anm'):
                path += '.anm'
            matches = [by_name[path]] if path in by_name else []
        valid = [i for i in matches if records[i].get('parse_status') != 'rejected']
        marker_checks = []
        for event in d['events']:
            present = [i for i in valid if any(a <= event['frame'] <= b
                       for a, b in records[i]['frame_coordinate_runs'])]
            in_bounds = [i for i in valid if 0 <= event['frame'] < records[i]['declared_frame_count']]
            marker_checks.append({'frame': event['frame'], 'name': event['name'],
                                  'stored_in_resource_indices': present,
                                  'in_bounds_resource_indices': in_bounds})
        result.append({'definition_index': d['index'], 'name': d['name'],
                       'expression': expression, 'archive_join_mode': mode,
                       'resource_indices': matches, 'parsed_resource_indices': valid,
                       'marker_checks': marker_checks})
    return result


def scan_archive(archive: zipfile.ZipFile, prefix: str, catalog: dict | None) -> dict:
    records, layouts, layout_index, directories = [], [], {}, set()
    for member in archive.namelist():
        parent = normalize(member).rstrip('/') if member.endswith('/') else posixpath.dirname(normalize(member))
        while parent:
            directories.add(parent)
            parent = posixpath.dirname(parent)
    for member in sorted(archive.namelist()):
        if not normalize(member).startswith(normalize(prefix).rstrip('/') + '/') or not member.lower().endswith('.anm'):
            continue
        data = archive.read(member)
        try:
            record = parse_anm(data)
        except ValueError as error:
            records.append({'member': member, 'parse_status': 'rejected',
                            'error': str(error), 'sha256': hashlib.sha256(data).hexdigest(),
                            'size': len(data)})
            continue
        record['parse_status'] = 'parsed'
        record['member'] = member
        layout = {k: record.pop(k) for k in ('bones', 'first_frame_pose_order')}
        key = json.dumps(layout, sort_keys=True)
        if key not in layout_index:
            layout_index[key] = len(layouts)
            layouts.append(layout)
        record['layout_index'] = layout_index[key]
        records.append(record)
    joined = join_declarations(catalog, records, prefix, directories) if catalog else []
    valid = [r for r in records if r['parse_status'] == 'parsed']
    counts = Counter()
    for r in valid:
        counts.update(r['counts'])
    marker_mismatches = []
    for declaration in joined:
        for marker in declaration['marker_checks']:
            missing = sorted(set(declaration['parsed_resource_indices'])
                             - set(marker['stored_in_resource_indices']))
            if missing:
                marker_mismatches.append({'definition_index': declaration['definition_index'],
                                          'name': declaration['name'], 'marker': marker['name'],
                                          'frame': marker['frame'], 'resource_indices': missing,
                                          'outside_declared_range': sorted(set(missing) - set(
                                              marker['in_bounds_resource_indices']))})
    duplicate_markers, rotations = [], []
    for declaration in catalog['definitions'] if catalog else []:
        counts_by_frame = Counter(e['frame'] for e in declaration['events'])
        for frame, count in counts_by_frame.items():
            if count > 1:
                duplicate_markers.append({'definition_index': declaration['index'],
                                          'name': declaration['name'], 'frame': frame,
                                          'markers': [e['name'] for e in declaration['events']
                                                      if e['frame'] == frame]})
        for prop in declaration['properties']:
            if prop['name'] != 'rotate':
                continue
            for i in joined[declaration['index']]['parsed_resource_indices']:
                bones = layouts[records[i]['layout_index']]['bones']
                bindings = [{'bone_index': n, 'track_index': b['track_index']}
                            for n, b in enumerate(bones) if b['name'] == prop['values'][0]]
                rotations.append({'definition_index': declaration['index'],
                                  'name': declaration['name'], 'resource_index': i,
                                  'bone_name': prop['values'][0], 'bindings': bindings})
    return {
        'schema_version': 1, 'steam_build_id': '25492057', 'runtime_verified': False,
        'scope': 'Bounded EANM metadata and archive-relative declaration join. No mounted '
                 'override precedence, iterator order, mesh decoding, interpolation or '
                 'native playback claimed. Directory matches are an unordered set.',
        'archive': Path(archive.filename).name, 'prefix_assumption': prefix,
        'counts': {'resources': len(records), 'parsed': len(valid),
                   'rejected': len(records) - len(valid), 'layouts': len(layouts),
                   'versions': dict(Counter(r['version'] for r in valid)),
                   'stored_frames': sum(r['stored_frame_count'] for r in valid),
                   'payloads': dict(sorted(counts.items())),
                   'declarations_joined': sum(bool(r['resource_indices']) for r in joined),
                   'declarations_unmatched': sum(not r['resource_indices'] for r in joined)},
        'layouts': layouts, 'resources': records, 'declaration_join': joined,
        'audit': {'rejected_resource_indices': [i for i, r in enumerate(records)
                                               if r['parse_status'] == 'rejected'],
                  'marker_coordinate_mismatches': marker_mismatches,
                  'duplicate_marker_coordinates': duplicate_markers,
                  'rotation_bone_bindings': rotations},
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive', type=Path, required=True)
    p.add_argument('--prefix', default='properties/animation/human')
    p.add_argument('--catalog', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    catalog = json.loads(args.catalog.read_text()) if args.catalog else None
    with zipfile.ZipFile(args.archive) as archive:
        output = scan_archive(archive, args.prefix, catalog)
    if args.catalog:
        output['catalog_sha256'] = hashlib.sha256(args.catalog.read_bytes()).hexdigest()
    args.output.write_text(json.dumps(output, indent=2) + '\n')
    print(json.dumps(output['counts'], indent=2))


if __name__ == '__main__':
    main()
