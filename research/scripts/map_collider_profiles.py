#!/usr/bin/env python3
"""Resolve the bounded base colliders.set profile inheritance and category masks.

Follows reviewed 724250/725160 contracts for the forms in this one member.
This is not mount resolution, a game loader, or evidence of live attachment.
Category numbers assume an initially empty native tagged-flags registry.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import struct
import zipfile
from pathlib import Path

from map_human_fsm import atoms, children, head, parse

MASKS = ('canpush', 'canpushbroken', 'useramvolume', 'nocontactwith', 'collidewith')
BOOLEANS = ('pointcollider', 'nocollideforstable', 'nocontactsatall')


def resolve(data: bytes) -> dict:
    roots = parse(data.decode('cp1251'), 'set/colliders.set')
    if len(roots) != 1 or head(roots[0]) != 'colliders':
        raise ValueError('Expected one Colliders root')
    profiles, by_name, categories = [], {}, []

    def category(name):
        if name not in categories:
            if len(categories) == 32:
                raise ValueError('Category count exceeds the native 32-bit registry')
            categories.append(name)
        return categories.index(name)

    for node in children(roots[0]):
        words = atoms(node)
        if node.delimiter != '{' or len(words) not in (1, 3):
            raise ValueError(f'Unsupported profile declaration at line {node.line}')
        name = words[0]
        if name in by_name:
            raise ValueError(f'Duplicate profile: {name}')
        parent = None
        defaults = {**{key: [] for key in MASKS}, **{key: False for key in BOOLEANS},
                    'reject_collider_by_volume': False, 'colliderid': None,
                    'blocking': False, 'speed': 0.0, 'penetration': 0.0}
        if len(words) == 3:
            if words[1] != 'from' or words[2] not in by_name:
                raise ValueError(f'Unresolved earlier parent for {name}')
            parent = words[2]
            defaults = copy.deepcopy(by_name[parent]['resolved'])
        for child in children(node):
            key, args = head(child), atoms(child)[1:]
            if child.delimiter != '{':
                raise ValueError('Includes/macros are outside this bounded member model')
            if key in MASKS and not children(child):
                # This resource uses ordinary quoted tag additions only. The
                # native parser also supports a bare clear token and -tag;
                # reject those here because this parser does not retain quotes.
                if any(tag == 'clear' or tag.startswith('-') for tag in args):
                    raise ValueError('clear/-tag form needs a quote-preserving parser')
                for tag in args:
                    category(tag)
                    if tag not in defaults[key]:
                        defaults[key].append(tag)
            elif key == 'colliderid' and len(args) == 1 and not children(child):
                category(args[0])
                defaults[key] = args[0]
            elif key in BOOLEANS and not children(child) and args in ([], ['0'], ['1']):
                defaults[key] = args != ['0']
            elif key == 'rejectcolliderbyvolume' and not args and not children(child):
                defaults['reject_collider_by_volume'] = True
            elif key == 'blocking' and not args:
                defaults['blocking'] = True
                for setting in children(child):
                    if head(setting) not in ('speed', 'penetration') or len(atoms(setting)) != 2 or children(setting):
                        raise ValueError('Unsupported Blocking setting')
                    defaults[head(setting)] = struct.unpack('<f', struct.pack('<f', float(atoms(setting)[1])))[0]
            else:
                raise ValueError(f'Unsupported collider property {key} at line {child.line}')
        row = {'name': name, 'line': node.line, 'parent': parent, 'resolved': defaults}
        profiles.append(row)
        by_name[name] = row
    for row in profiles:
        state = row['resolved']
        row['category_index_if_initially_empty'] = categories.index(state['colliderid']) if state['colliderid'] is not None else -1
        row['masks_if_initially_empty'] = {key: f'0x{sum(1 << categories.index(tag) for tag in state[key]):08x}' for key in MASKS}
    return {'schema_version': 1, 'steam_build_id': '25492057', 'runtime_verified': False,
            'scope': 'One base gamelogic.pak member; sequential prior-parent inheritance, native defaults and additive masks. '
                     'Category numbers are a model for an initially empty registry, not live observations. '
                     'No include/mount/mod/attachment resolution. Native clear/-tag forms rejected because absent from this member.',
            'resource_members': [{'archive': 'gamelogic.pak', 'member': 'set/colliders.set',
                                  'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}],
            'profiles': profiles, 'category_insertion_order_if_initially_empty': categories,
            'native_default_profile': profiles[0]['name'] if profiles else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with zipfile.ZipFile(args.resources_dir / 'gamelogic.pak') as archive:
        result = resolve(archive.read('set/colliders.set'))
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'profiles': len(result['profiles']),
                      'categories': len(result['category_insertion_order_if_initially_empty']),
                      'default': result['native_default_profile']}, indent=2))


if __name__ == '__main__':
    main()
