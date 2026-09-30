#!/usr/bin/env python3
"""Audit base path profiles and obstacle costs without executing game code.

Only the local named-argument SDL macros in these resources are expanded. The
native contracts are reviewed in path-profiles-map-25492057.md. This does not
resolve mounted overrides, registry iteration order or engine
floating-point rounding. Negative obstacle declarations leave the previous
value in place; they do not store a negative float.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import zipfile
from pathlib import Path

from map_human_fsm import atoms, children, head, parse, shape
from map_idle_policy import Expander

PROFILE_FLAGS = {'network': 1, 'linearpath': 2, 'human': 4,
                 'battlecrusher': 8, 'skipimpregnable': 16}
OBSTACLE_FLAGS = {'flag_damage': 1, 'flag_human': 2,
                  'flag_crush_in_battle': 4, 'flag_skip_impregnable': 8,
                  'flag_vaultable': 16}
TERRAIN_IDS = dict(enumerate(('undef', 'ground', 'no_pass', 'shallow_water',
                              'water', 'deep_water', 'swamp', 'road',
                              'no_pass_human', 'snow', 'forest', 'slope')))
TERRAIN_NAMES = {name: index for index, name in TERRAIN_IDS.items()}
EPSILON = struct.unpack('<f', struct.pack('<f', 1e-5))[0]


def finite(value: str) -> float:
    result = struct.unpack('<f', struct.pack('<f', float(value)))[0]
    if not math.isfinite(result):
        raise ValueError('Nonfinite authored number')
    return result


def expand(text: str, member: str):
    expander = Expander()
    nodes = expander.expand(parse(text, member))
    return nodes, expander


def profiles(text: str, member: str) -> dict:
    nodes, expander = expand(text, member)
    if len(nodes) != 1 or head(nodes[0]) != 'pathers':
        raise ValueError('Expected one Pathers section')
    shape(nodes[0], (1,), allow_children=True)
    result = []
    names = set()
    for node in children(nodes[0]):
        shape(node, (2,), allow_children=True)
        if head(node) != 'profile':
            raise ValueError('Unexpected profile section')
        name = atoms(node)[1]
        if name in names or len(result) >= 255:
            raise ValueError('Duplicate profile name or byte ID limit')
        names.add(name)
        row = {'name': name, 'declaration_index': len(result),
               'member': node.source, 'line': node.line, 'flags': 0,
               'radius_authored': None, 'radius_native_default': 30.0,
               'priority': 0, 'height_costs': [0.0] * 15,
               'terrain_declarations': [],
               'terrain_costs_model': [-1.0] + [0.0] * 15,
               'terrain_ratios': [-1.0] + [1.0] * 15,
               'terrain_permissions': [True] * 16}
        for prop in children(node):
            key, values = head(prop), atoms(prop)[1:]
            if key in PROFILE_FLAGS:
                shape(prop, (1,))
                row['flags'] |= PROFILE_FLAGS[key]
            elif key == 'radius':
                shape(prop, (2,))
                row['radius_authored'] = finite(values[0])
            elif key == 'priority':
                shape(prop, (2,))
                row['priority'] = int(values[0])
            elif key == 'height':
                shape(prop, (16,))
                row['height_costs'] = [finite(v) for v in values]
            elif key == 'terrain':
                if children(prop):
                    raise ValueError('Nested Terrain row')
                i = 0
                while i < len(values):
                    if i + 1 == len(values):
                        raise ValueError('Missing terrain ratio')
                    terrain, raw = values[i:i + 2]
                    if terrain not in TERRAIN_NAMES:
                        raise ValueError(f'Unknown terrain name {terrain}')
                    terrain_id = TERRAIN_NAMES[terrain]
                    ratio = max(0.0, min(1.0, finite(raw)))
                    i += 2
                    permission = True
                    # The optional native argument is an integer, not a word.
                    if i < len(values):
                        try:
                            permission = int(values[i]) != 0
                        except ValueError:
                            pass
                        else:
                            i += 1
                    cost = -1.0 if ratio < EPSILON else 1 / math.sqrt(ratio) - 1
                    stored_ratio = -1.0 if ratio < EPSILON else ratio
                    row['terrain_declarations'].append({
                        'name': terrain, 'id': terrain_id, 'authored_ratio': finite(raw),
                        'native_ratio_model': stored_ratio,
                        'cost_model': cost,
                        'permission': permission,
                    })
                    row['terrain_costs_model'][terrain_id] = cost
                    row['terrain_ratios'][terrain_id] = stored_ratio
                    row['terrain_permissions'][terrain_id] = permission
            else:
                raise ValueError(f'Unsupported profile property {key}')
        result.append(row)
    return {'profiles': result, 'macro_names': sorted(expander.macros),
            'macro_invocations': expander.invocations}


def obstacles(text: str, member: str, profile_rows: list[dict]) -> dict:
    nodes, expander = expand(text, member)
    if len(nodes) != 1 or head(nodes[0]) != 'obstacles':
        raise ValueError('Expected one obstacles section')
    shape(nodes[0], (1,), allow_children=True)
    result, names = [], set()
    for node in children(nodes[0]):
        shape(node, (1,), allow_children=True)
        name = atoms(node)[0]
        if name in names:
            raise ValueError('Duplicate obstacle name')
        names.add(name)
        row = {'name': name, 'source_order': len(result), 'member': node.source,
               'line': node.line, 'flags': 0,
               'costs': {p['name']: 100000.0 for p in profile_rows},
               'cost_declarations': []}
        for prop in children(node):
            words = atoms(prop)
            if head(prop) in OBSTACLE_FLAGS:
                shape(prop, (1,))
                row['flags'] |= OBSTACLE_FLAGS[head(prop)]
            else:
                shape(prop, (2,))
                pid, value = words[0], int(words[1])
                if pid not in row['costs']:
                    raise ValueError(f'Unknown profile {pid} in obstacle {name}')
                row['cost_declarations'].append({'profile': pid, 'value': value})
                if value >= 0:
                    row['costs'][pid] = float(value)
        result.append(row)
    return {'obstacles': result, 'macro_names': sorted(expander.macros),
            'macro_invocations': expander.invocations}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with zipfile.ZipFile(args.archive) as archive:
        resources = []
        raw = {}
        for member in ('set/PATHER.SET', 'set/registry/obstacles.reg'):
            data = archive.read(member)
            raw[member] = data.decode('cp1251')
            resources.append({'archive': args.archive.name, 'member': member,
                              'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    p = profiles(raw['set/PATHER.SET'], 'set/PATHER.SET')
    o = obstacles(raw['set/registry/obstacles.reg'], 'set/registry/obstacles.reg', p['profiles'])
    result = {'schema_version': 1, 'steam_build_id': '25492057', 'runtime_verified': False,
              'scope': 'Base declarations after bounded local macro expansion. No mounted '
                       'overrides or live profile/obstacle IDs assumed. The terrain ID '
                       'table is initialized natively by A44070/8CC950. Profile declaration order feeds the '
                       'native vector; obstacle source_order is not a verified native ID. '
                       'Inputs and epsilon use binary32; derived cost models use Python '
                       'sqrt/division, not bit-exact native powf rounding. '
                       'Unspecified terrain indices retain native defaults: index0 cost/ratio '
                       '-1, indices1..15 cost0/ratio1, all permissions true. Radius authored '
                       'values require the native spatial scale.',
              'resources': resources, 'terrain_ids': TERRAIN_IDS,
              'profile_data': p, 'obstacle_data': o}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'profiles': len(p['profiles']), 'obstacles': len(o['obstacles']),
                      'matrix_entries': len(p['profiles']) * len(o['obstacles']),
                      'vaultable': [r['name'] for r in o['obstacles'] if r['flags'] & 16]}))


if __name__ == '__main__':
    main()
