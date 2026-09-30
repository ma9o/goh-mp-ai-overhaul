#!/usr/bin/env python3
"""Audit base sound-folder inheritance without mounting or playing resources.

Supports the literal, zero-argument macros/includes found in the seven base
sound archives. Unknown syntax, conflicting resources, unresolved macros and
cycles fail closed. Effective fields model the reviewed native folder copy and
ordered overrides; audio and signal distances retain their authored inputs.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import posixpath
import zipfile
from collections import Counter, defaultdict
from contextlib import ExitStack
from pathlib import Path

from map_human_animations import parse
from map_human_fsm import atoms, children, shape


BASE_ARCHIVES = ('detonation.pak', 'mission.pak', 'sound.pak', 'talk.pak',
                 'vehicle.pak', 'weapon.pak', 'world.pak')
SIGNAL_TYPES = {'movement', 'shot', 'hit', 'injure', 'explosion', 'mandie',
                'sound', 'ally_info'}


def digest(value) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True,
                                     separators=(',', ':')).encode()).hexdigest()


def number(value: str) -> float:
    result = float(value)
    if not math.isfinite(result):
        raise ValueError('Non-finite property')
    return result


def member_path(path: str, source: str = '') -> str:
    path = path.replace('\\', '/')
    if path.startswith('/sound/'):
        path = path[len('/sound/'):]
    elif path.startswith('/'):
        raise ValueError(f'Include outside reviewed sound root: {path}')
    else:
        path = posixpath.join(posixpath.dirname(source), path)
    result = posixpath.normpath(path)
    if result in ('', '.', '..') or result.startswith('../'):
        raise ValueError(f'Invalid sound member: {path}')
    return result


class Resources:
    def __init__(self, archives: dict[str, zipfile.ZipFile]):
        self.archives = archives
        self.index, self.resources = {}, {}
        for archive, handle in archives.items():
            for info in handle.infolist():
                if info.is_dir():
                    continue
                name = member_path(info.filename)
                if name in self.index:
                    raise ValueError(f'Ambiguous mounted resource: {name}')
                self.index[name] = (archive, info)
        folded = Counter(n.casefold() for n in self.index)
        if any(v != 1 for v in folded.values()):
            raise ValueError('Case-colliding sound resource names')

    def read(self, member: str):
        if member not in self.index:
            raise ValueError(f'Missing included resource: {member}')
        archive, info = self.index[member]
        data = self.archives[archive].read(info)
        self.resources[member] = {'archive': archive, 'member': info.filename,
                                  'size': len(data),
                                  'sha256': hashlib.sha256(data).hexdigest()}
        if b'\0' in data:
            raise ValueError(f'Binary text resource: {member}')
        return parse(data.decode('cp1251'), member)


class Expander:
    def __init__(self, resources: Resources):
        self.resources = resources
        self.definitions = {}
        self.calls = Counter()

    def expand(self, nodes, includes=(), macros=()):
        for node in nodes:
            words = atoms(node)
            if node.delimiter == '{':
                if children(node):
                    raise ValueError(f'Nested sound property: {words}')
                yield node, macros
            elif words[0] == 'include':
                shape(node, (2,))
                member = member_path(words[1], node.source)
                if member in includes:
                    raise ValueError(f'Cyclic sound include: {member}')
                yield from self.expand(self.resources.read(member),
                                       includes + (member,), macros)
            elif words[0] == 'define':
                shape(node, (2,), allow_children=True)
                name = words[1]
                if name in self.definitions:
                    old = self.definitions[name]
                    # Re-including the identical source definition is harmless
                    # to this literal model; conflicting definitions are not.
                    if (old.source, old.line, old.items) != (node.source, node.line, node.items):
                        raise ValueError(f'Conflicting macro definition: {name}')
                self.definitions[name] = node
            else:
                shape(node, (1,))
                name = words[0]
                if name in macros:
                    raise ValueError(f'Cyclic sound macro: {name}')
                if name not in self.definitions:
                    raise ValueError(f'Unresolved sound macro: {name}')
                self.calls[name] += 1
                yield from self.expand(children(self.definitions[name]), includes,
                                       macros + (name,))

    def folder(self, member: str):
        roots = self.resources.read(member)
        if len(roots) != 1 or roots[0].delimiter != '{' or atoms(roots[0]) != ['sound']:
            raise ValueError(f'Expected one sound root: {member}')
        return list(self.expand(children(roots[0]), (member,)))


def loudness_catalog(resources: Resources):
    roots = resources.read('sound.set')
    if len(roots) != 1 or atoms(roots[0]) != ['sound']:
        raise ValueError('Expected sound.set root')
    records, percent = [], []
    for node in children(roots[0]):
        words = atoms(node)
        if node.delimiter != '{':
            raise ValueError('Unreviewed sound.set preprocessing')
        if words[0] == 'loudness':
            shape(node, (5,))
            records.append({'name': words[1], 'min_distance_input': number(words[2]),
                            'max_distance_input': number(words[3]),
                            'priority_input': int(words[4]), 'line': node.line})
        elif words[0] == 'percent':
            shape(node, (3, 4))
            percent.append({'group': words[1], 'arguments': words[2:], 'line': node.line})
        else:
            raise ValueError(f'Unreviewed sound.set property: {words[0]}')
    return records, percent


def resolve_loudness(records, name):
    # Loading updates an existing case-insensitive row in place. All reviewed
    # declarations specify all three values, so the last declaration wins.
    # Subsequent native lookup returns that updated row, or the default row.
    for row in reversed(records):
        if row['name'].casefold() == name.casefold():
            return row, False
    for row in reversed(records):
        if row['name'].casefold() == 'default':
            return row, True
    raise ValueError('Missing default loudness')


def defaults():
    return {'group': 4, 'min_distance': {'native_default': 0},
            'max_distance': {'native_default': 100}, 'priority': -1,
            'signal': {'type': None, 'radius_input': 0},
            'signal_period': -1, 'volume': 1}


def apply(values, provenance, node, chain, folder, loudness):
    words = atoms(node)
    key = words[0].casefold()
    source = {'folder': folder, 'member': node.source, 'line': node.line}
    if chain:
        source['macro_chain'] = list(chain)
    updates = {}
    if key == 'signal':
        shape(node, (3,))
        if words[1].casefold() not in SIGNAL_TYPES:
            raise ValueError(f'Unreviewed signal name: {words[1]}')
        updates['signal'] = {'type': words[1].casefold(), 'radius_input': number(words[2])}
    elif key in ('volume', 'signalperiod'):
        shape(node, (2,))
        updates['signal_period' if key == 'signalperiod' else key] = number(words[1])
    elif key == 'tag':
        shape(node, (2,))
        updates['group'] = {'effects': 0, 'emotions': 1, 'speech': 2,
                            'environment': 3}.get(words[1].casefold(), 4)
    elif key == 'loudness':
        shape(node, (2,))
        row, fallback = resolve_loudness(loudness, words[1])
        updates = {'min_distance': {'input': row['min_distance_input']},
                   'max_distance': {'input': row['max_distance_input']},
                   'priority': row['priority_input']}
        source.update({'loudness_requested': words[1], 'loudness_line': row['line'],
                       'loudness_fallback': fallback})
    elif key in ('distance', 'mindistance', 'maxdistance'):
        shape(node, (3,) if key == 'distance' else (2,))
        if key != 'maxdistance':
            updates['min_distance'] = {'input': number(words[1])}
        if key != 'mindistance':
            updates['max_distance'] = {'input': number(words[-1])}
    else:
        raise ValueError(f'Unreviewed sound-folder property: {words[0]}')
    values.update(updates)
    for field in updates:
        provenance[field] = source
    return {'property': key, 'updates': updates, 'source': source}


def audit(resources: Resources):
    loudness, percent = loudness_catalog(resources)
    declarations = {posixpath.dirname(n): n for n in resources.index
                    if posixpath.basename(n).casefold() == 'folder.set'}
    if '' not in declarations:
        raise ValueError('Missing base sound/folder.set')
    compiled, calls = {}, Counter()
    for folder in sorted(declarations, key=lambda p: (p.count('/'), bool(p), p)):
        parent = posixpath.dirname(folder)
        while parent not in compiled and parent:
            parent = posixpath.dirname(parent)
        values = copy.deepcopy(compiled[parent]['effective']) if folder else defaults()
        provenance = copy.deepcopy(compiled[parent]['provenance']) if folder else {}
        if not folder:
            row, _ = resolve_loudness(loudness, 'default')
            values['min_distance'] = {'input': row['min_distance_input']}
            values['max_distance'] = {'input': row['max_distance_input']}
        expander = Expander(resources)
        assignments = [apply(values, provenance, n, chain, folder, loudness)
                       for n, chain in expander.folder(declarations[folder])]
        calls.update(expander.calls)
        compiled[folder] = {'path': folder, 'inherited_declaration': parent if folder else None,
                            'member': declarations[folder], 'effective': values,
                            'provenance': provenance, 'assignments': assignments}
    directories = defaultdict(list)
    for name, (archive, info) in resources.index.items():
        if name.endswith('.wav'):
            directories[posixpath.dirname(name)].append({
                'archive': archive, 'member': name, 'size': info.file_size, 'crc32': info.CRC})
        elif name.casefold().endswith('.wav'):
            raise ValueError(f'Unreviewed WAV extension case: {name}')
    assets = []
    for folder, rows in sorted(directories.items()):
        definition = folder
        while definition not in compiled and definition:
            definition = posixpath.dirname(definition)
        rows.sort(key=lambda r: (r['archive'], r['member']))
        assets.append({'path': folder, 'immediate_wav_count': len(rows),
                       'effective_declaration': definition,
                       'central_directory_metadata_sha256': digest(rows)})
    names = Counter(r['name'].casefold() for r in loudness)
    return {'schema_version': 1, 'steam_build_id': '25492057', 'runtime_verified': False,
            'scope': 'Seven base resource/sound archives treated as one unique namespace; '
                     'no mount order, DLC/mod overrides, playback, RNG outcome or source lifetime proof.',
            'units': 'Distance input values are multiplied by mutable spatial scale14776C0 '
                     '(file initial20). Period inputs use the simulation-clock converter. '
                     'Native constructor distances are explicitly distinguished from input values.',
            'asset_inventory_scope': 'Only immediate .wav names/size/CRC from ZIP central directories. '
                                     'Per-directory digest is metadata, not a WAV-content hash or native iteration order.',
            'counts': {'folder_declarations': len(compiled), 'asset_directories': len(assets),
                       'wav_members': sum(len(v) for v in directories.values()),
                       'expanded_macro_calls': sum(calls.values()), 'used_macros': len(calls),
                       'loudness_rows': len(loudness),
                       'unique_loudness_names_casefolded': len(names)},
            'macro_calls': dict(sorted(calls.items())),
            'loudness_rows': loudness, 'percent_declarations': percent,
            'duplicate_loudness_names_casefolded': {k: v for k, v in names.items() if v > 1},
            'folders': list(compiled.values()), 'asset_directories': assets,
            'resources': sorted(resources.resources.values(), key=lambda r: (r['archive'], r['member']))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with ExitStack() as stack:
        archives = {'sound/' + name: stack.enter_context(zipfile.ZipFile(
            args.resources_dir / 'sound' / name)) for name in BASE_ARCHIVES}
        result = audit(Resources(archives))
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result['counts'], indent=2))


if __name__ == '__main__':
    main()
