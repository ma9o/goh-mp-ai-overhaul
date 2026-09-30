#!/usr/bin/env python3
"""Inventory literal bounding-shape declarations in installed base archives.

This is a bounded text census, not mount resolution or a native parser model.
It does not expand macros/includes or inspect map archives, mods or binaries.
The existing conservative SDL parser rejects unfamiliar forms explicitly.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import zipfile
from collections import Counter
from pathlib import Path

from map_human_fsm import Node, atoms, children, head, parse

KINDS = ('box2', 'circle2', 'capsule2', 'obb2', 'polygon2', 'sphere3',
         'capsule3', 'lozenge3', 'compoundarea', 'compoundvolume')
PATTERN = re.compile(rb'\b(?:' + b'|'.join(x.encode() for x in KINDS) + rb')\b', re.I)
EXTENSIONS = {'.def', '.inc', '.set', '.mdl', '.vol', '.ext', '.reg',
              '.weapon', '.ammo', '.grenade', '.mine'}
ARCHIVES = ('gamelogic.pak', 'properties.pak', 'entity/-vehicle.pak',
            'entity/construction.pak', 'entity/entity.pak', 'entity/flora.pak',
            'entity/humanskin.pak', 'entity/inventory.pak', 'entity/landscape.pak',
            'dlc1.pak', 'dlc2.pak', 'dlc3.pak', 'dlc4.pak')


def walk(nodes: list[Node], parents: tuple = ()):
    for node in nodes:
        name = head(node)
        if node.delimiter == '{' and name in KINDS:
            yield node, parents
        yield from walk(children(node), parents + (name,))


def numbers(values):
    result = [struct.unpack('<f', struct.pack('<f', float(v)))[0] for v in values]
    if not all(math.isfinite(v) for v in result):
        raise ValueError('Nonfinite value')
    return result


def geometry_checks(node):
    """Check authored inputs, without asserting a runtime defect or reachability."""
    props = {}
    issues = []
    for child in children(node):
        props.setdefault(head(child), []).append(atoms(child)[1:])
    try:
        if head(node) == 'obb2':
            axis = numbers(props.get('axis', [['1', '0']])[-1])
            extent = numbers(props.get('extent', [['5', '5']])[-1])
            if len(axis) != 2 or len(extent) != 2:
                raise ValueError('Unexpected OBB arity')
            norm2 = sum(x*x for x in axis)
            if abs(norm2 - 1) > 1e-4:
                issues.append({'kind': 'obb_axis_squared_norm_differs_from_one',
                               'axis': axis, 'norm_squared': norm2})
            if min(extent) < 0:
                issues.append({'kind': 'negative_obb_extent', 'extent': extent})
        elif head(node) == 'polygon2':
            vertices = [numbers(v) for v in props.get('vertex', [])]
            if any(len(v) != 2 for v in vertices):
                raise ValueError('Unexpected vertex arity')
            if len(vertices) < 3:
                issues.append({'kind': 'fewer_than_three_polygon_vertices',
                               'count': len(vertices)})
            if len({tuple(v) for v in vertices}) != len(vertices):
                issues.append({'kind': 'duplicate_polygon_vertices', 'vertices': vertices})
            if len(vertices) >= 3:
                turns = []
                for i, a in enumerate(vertices):
                    b, c = vertices[(i+1) % len(vertices)], vertices[(i+2) % len(vertices)]
                    turns.append((b[0]-a[0])*(c[1]-a[1]) - (b[1]-a[1])*(c[0]-a[0]))
                if any(t > 0 for t in turns) and any(t < 0 for t in turns):
                    issues.append({'kind': 'mixed_polygon_turn_signs', 'vertices': vertices})
        for values in props.get('radius', []):
            if min(numbers(values)) < 0:
                issues.append({'kind': 'negative_radius', 'values': values})
    except (ValueError, OverflowError, struct.error) as error:
        issues.append({'kind': 'geometry_check_unresolved', 'reason': str(error)})
    return issues


def catalog(root: Path):
    result = {'schema_version': 1, 'steam_build_id': '25492057', 'runtime_verified': False,
              'scope': 'Fixed 13 base archives and 11 extensions. Literal census includes '
                       'comments/strings. Parsed counts require a complete successful conservative '
                       'SDL parse, exclude comments, and do not expand macros/includes or resolve '
                       'mounts. Geometry checks use binary32 inputs and Python arithmetic, '
                       'not native predicate evaluation. No map archives or mods.',
              'archives': [], 'matching_members': [], 'parse_failures': [],
              'geometry_findings': [], 'parsed_shape_counts': {}, 'parsed_parent_counts': {}}
    all_shapes, all_parents = Counter(), Counter()
    for name in ARCHIVES:
        count, size, literals = 0, 0, Counter()
        digest = hashlib.sha256()
        with zipfile.ZipFile(root / name) as archive:
            entries = sorted((i for i in archive.infolist()
                              if Path(i.filename).suffix.lower() in EXTENSIONS),
                             key=lambda i: i.filename)
            if len({i.filename for i in entries}) != len(entries):
                raise ValueError(f'Duplicate member names in {name}')
            for info in entries:
                data = archive.read(info)
                member_hash = hashlib.sha256(data).hexdigest()
                digest.update(json.dumps([info.filename, len(data), member_hash],
                                         ensure_ascii=True, separators=(',', ':')).encode() + b'\n')
                count += 1
                size += len(data)
                matches = Counter(m.group().decode().lower() for m in PATTERN.finditer(data))
                if not matches:
                    continue
                literals.update(matches)
                row = {'archive': name, 'member': info.filename, 'size': len(data),
                       'sha256': member_hash, 'literal_counts': dict(sorted(matches.items()))}
                result['matching_members'].append(row)
                try:
                    if b'\0' in data:
                        raise ValueError('NUL in text candidate')
                    nodes = parse(data.decode('cp1251'), info.filename)
                    shapes = list(walk(nodes))
                except (ValueError, UnicodeError) as error:
                    result['parse_failures'].append({'archive': name, 'member': info.filename,
                                                     'reason': str(error)})
                    row['parsed'] = False
                    continue
                row['parsed'] = True
                counts = Counter(head(node) for node, _ in shapes)
                row['parsed_shape_counts'] = dict(sorted(counts.items()))
                all_shapes.update(counts)
                for node, parents in shapes:
                    all_parents['/'.join(parents[-2:]) or '<root>'] += 1
                    for issue in geometry_checks(node):
                        result['geometry_findings'].append({'archive': name, 'member': info.filename,
                                                           'line': node.line, 'shape': head(node),
                                                           'parent_path': list(parents),
                                                           **issue})
        result['archives'].append({'archive': name, 'extensions': sorted(EXTENSIONS),
                                   'members_scanned': count, 'bytes_scanned': size,
                                   'scanned_member_digest_sha256': digest.hexdigest(),
                                   'digest_format': 'Sorted UTF-8 JSON [name,size,sha256] plus LF per scanned member',
                                   'literal_counts': dict(sorted(literals.items()))})
    result['parsed_shape_counts'] = dict(sorted(all_shapes.items()))
    result['parsed_parent_counts'] = dict(sorted(all_parents.items()))
    result['geometry_finding_counts'] = dict(sorted(Counter(
        x['kind'] for x in result['geometry_findings']).items()))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = catalog(args.resources_dir)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'members_scanned': sum(a['members_scanned'] for a in result['archives']),
                      'matching_members': len(result['matching_members']),
                      'parse_failures': len(result['parse_failures']),
                      'parsed_shape_counts': result['parsed_shape_counts'],
                      'geometry_finding_counts': result['geometry_finding_counts']}, indent=2))


if __name__ == '__main__':
    main()
