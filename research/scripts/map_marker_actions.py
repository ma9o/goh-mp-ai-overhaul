#!/usr/bin/env python3
"""Extract base human marker handlers and their literal call/macro dependencies.

Preserves ordered SDL syntax; does not execute or expand macros, bind inherited
event layers, resolve mounted overrides, or assume names imply gameplay effects.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import zipfile
from collections import Counter, deque
from pathlib import Path

from map_human_animations import parse
from map_human_fsm import Node, atoms, children, head


def walk(node):
    yield node
    for child in children(node):
        yield from walk(child)


def encode(node):
    return {'delimiter': node.delimiter, 'member': node.source, 'line': node.line,
            'items': [encode(x) if isinstance(x, Node) else x for x in node.items]}


def extract(text: str, member: str, animation_catalog: dict) -> dict:
    roots = parse(text, member)
    if len(roots) != 1 or atoms(roots[0]) != ['human']:
        raise ValueError('Expected one human interaction root')
    events, macros, markers, initial_sound_links = {}, {}, [], []
    for node in children(roots[0]):
        words = atoms(node)
        if node.delimiter == '(' and head(node) == 'define':
            if len(words) != 2 or words[1] in macros:
                raise ValueError('Ambiguous top-level macro')
            macros[words[1]] = node
        elif words[:2] == ['on', 'animation_event']:
            if len(words) < 3:
                raise ValueError('Missing marker name')
            markers.append(node)
        elif words[0] == 'on' and len(words) == 2:
            if words[1] in events:
                raise ValueError('Ambiguous simple event declaration')
            events[words[1]] = node
            if words[1] == 'spawn':
                initial_sound_links.extend(n for n in children(node) if head(n) == 'link_sound')
    pending = deque(('marker', str(i), n) for i, n in enumerate(markers))
    seen, handlers, edges, unresolved = set(), [], [], []
    action_counts, macro_calls = Counter(), []
    while pending:
        kind, name, node = pending.popleft()
        key = (kind, name)
        if key in seen:
            continue
        seen.add(key)
        handlers.append({'kind': kind, 'name': atoms(node)[2] if kind == 'marker' else name,
                         'syntax': encode(node)})
        for child in walk(node):
            words = atoms(child)
            if child is node:
                continue
            target_kind = target_name = None
            if child.delimiter == '(':
                target_kind, target_name = 'macro', words[0]
                macro_calls.append({'name': target_name, 'arguments': words[1:],
                                    'member': child.source, 'line': child.line})
            else:
                action_counts[head(child)] += 1
                if head(child) in ('call', 'call_synced'):
                    if len(words) != 2:
                        raise ValueError('Unsupported literal call shape')
                    target_kind, target_name = 'event', words[1]
            if target_kind:
                table = macros if target_kind == 'macro' else events
                edge = {'from_kind': kind, 'from_name': handlers[-1]['name'],
                        'to_kind': target_kind, 'to_name': target_name,
                        'member': child.source, 'line': child.line}
                edges.append(edge)
                if target_name in table:
                    pending.append((target_kind, target_name, table[target_name]))
                else:
                    unresolved.append(edge)
    bindings = Counter(atoms(n)[2] for n in markers)
    declared = Counter(e['name'] for d in animation_catalog['definitions'] for e in d['events'])
    return {'handlers': handlers, 'dependency_edges': edges, 'unresolved_dependencies': unresolved,
            'macro_invocations_unexpanded': macro_calls,
            'spawn_sound_links': [encode(n) for n in initial_sound_links],
            'counts': {'marker_handlers': len(markers), 'reached_handlers_and_macros': len(handlers),
                       'action_node_names': dict(sorted(action_counts.items()))},
            'marker_join': [{'name': name, 'animation_declaration_rows': declared[name],
                             'human_handler_count': bindings[name]}
                            for name in sorted(declared.keys() | bindings.keys())]}


def extract_sound_declarations(archive: zipfile.ZipFile) -> dict:
    names = ['folder.set', 'human/folder.set', 'human/move/folder.set',
             'human/move/fast/folder.set', 'human/move/normal/folder.set',
             'human/move/lying/folder.set', 'human/anims/folder.set',
             'human/anims/prone_to_stand/folder.set',
             'human/move/gear_rattle/normal/folder.set',
             'human/move/gear_rattle/fast/folder.set', 'defines.inc']
    selected = {'inf_anims', 'crawl', 'run', 'walk', 'footsteps', 'gear_rattle',
                'gear_rattle_normal', 'gear_rattle_fast', 'swim'}
    resources, definitions, folders = [], [], []
    for member in names:
        data = archive.read(member)
        resources.append({'archive': 'sound/sound.pak', 'member': member,
                          'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        roots = parse(data.decode('cp1251'), member)
        if member == 'defines.inc':
            for node in roots:
                words = atoms(node)
                if words[:1] == ['define'] and words[1] in selected:
                    definitions.append({'name': words[1], 'line': node.line,
                                        'properties': [{'atoms': atoms(c), 'line': c.line}
                                                       for c in children(node)]})
        else:
            if len(roots) != 1 or atoms(roots[0]) != ['sound']:
                raise ValueError('Unexpected sound folder declaration')
            folders.append({'member': member, 'directives': [
                {'delimiter': c.delimiter, 'atoms': atoms(c), 'line': c.line}
                for c in children(roots[0])]})
    if {r['name'] for r in definitions} != selected:
        raise ValueError('Missing reviewed sound macro')
    return {'schema_version': 1, 'runtime_verified': False,
            'scope': 'Selected sound-folder declarations and macro bodies; not native '
                     'inheritance, asset selection or mounted override resolution.',
            'resources': resources, 'definitions': definitions, 'folders': folders}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive', type=Path, required=True)
    p.add_argument('--catalog', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--sound-archive', type=Path)
    p.add_argument('--sound-output', type=Path)
    args = p.parse_args()
    if bool(args.sound_archive) != bool(args.sound_output):
        p.error('--sound-archive and --sound-output must be used together')
    member = 'set/interaction_entity/human.inc'
    with zipfile.ZipFile(args.archive) as archive:
        data = archive.read(member)
    result = extract(data.decode('cp1251'), member, json.loads(args.catalog.read_text()))
    result.update({'schema_version': 1, 'steam_build_id': '25492057', 'runtime_verified': False,
                   'scope': 'Direct base human marker handlers and literal call/macro closure. '
                            'Ordered syntax only: macros unexpanded, native behavior and '
                            'inherited/mounted overrides not inferred from names.',
                   'resource': {'archive': args.archive.name, 'member': member,
                                'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()},
                   'animation_catalog_sha256': hashlib.sha256(args.catalog.read_bytes()).hexdigest()})
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    if args.sound_archive:
        with zipfile.ZipFile(args.sound_archive) as archive:
            sounds = extract_sound_declarations(archive)
        args.sound_output.write_text(json.dumps(sounds, indent=2) + '\n')
    print(json.dumps({'counts': result['counts'], 'missing_base_handlers': [r['name']
                      for r in result['marker_join'] if not r['human_handler_count']],
                     'unresolved_dependencies': result['unresolved_dependencies']}, indent=2))


if __name__ == '__main__':
    main()
