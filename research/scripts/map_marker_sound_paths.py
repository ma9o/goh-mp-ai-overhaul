#!/usr/bin/env python3
"""Resolve sound path expressions in the bounded human marker closure.

Normalizes positional macro arguments only when their values reach emitted
syntax. Records unused arguments rather than inventing values for them. This is
a dependency audit, not a full SDL preprocessor, branch evaluator or live alias
simulation. Uses the prior native sound and marker maps for interpretation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import posixpath
import re
import zipfile
from collections import Counter, defaultdict
from contextlib import ExitStack
from pathlib import Path

from map_sound_folders import BASE_ARCHIVES, Resources, digest, member_path


def words(node):
    return [x for x in node['items'] if isinstance(x, str)]


def children(node):
    return [x for x in node['items'] if isinstance(x, dict)]


def walk(node):
    yield node
    for child in children(node):
        yield from walk(child)


class PositionalExpressions:
    def __init__(self, macros):
        self.macros, self.calls = macros, []

    def resolve(self, value, frame):
        def replace(match):
            index = int(match[1])
            if frame is None or index >= len(frame['args']):
                raise ValueError(f'Used macro argument has no binding: {value}')
            frame['used'].add(index)
            text, outer = frame['args'][index]
            return self.resolve(text, outer)
        return re.sub(r'%([0-9]+)', replace, value)

    def expand(self, node, frame=None, stack=()):
        raw = words(node)
        if node['delimiter'] == '(':
            name = raw[0]
            if name not in self.macros or children(node):
                raise ValueError(f'Unreviewed positional macro: {raw}')
            if len(raw) > 1 and raw[1] != 'args':
                raise ValueError(f'Unreviewed macro argument form: {raw}')
            if name in stack:
                raise ValueError(f'Cyclic positional macro: {name}')
            arguments = raw[2:] if len(raw) > 1 else []
            call = {'index': len(self.calls), 'name': name, 'raw_arguments': arguments,
                    'member': node['member'], 'line': node['line']}
            self.calls.append(call)
            inner = {'args': [(a, frame) for a in arguments], 'used': set(),
                     'ancestry': (frame['ancestry'] if frame else []) + [call['index']]}
            result = []
            for child in children(self.macros[name]):
                result.extend(self.expand(child, inner, stack + (name,)))
            call['used_arguments'] = {str(i): self.resolve(arguments[i], frame)
                                      for i in sorted(inner['used'])}
            call['unused_argument_indices'] = [i for i in range(len(arguments))
                                                if i not in inner['used']]
            return result
        if node['delimiter'] != '{':
            raise ValueError('Unknown syntax delimiter')
        parts = []
        for item in node['items']:
            if isinstance(item, str):
                parts.append(self.resolve(item, frame))
            else:
                parts.extend(self.expand(item, frame, stack))
        return [{**node, 'items': parts,
                 'macro_call_ids': frame['ancestry'] if frame else []}]


class SoundIndex:
    def __init__(self, resources, catalog):
        self.resources = resources
        self.folders = {r['path']: r for r in catalog['folders']}
        self.directories = {''}
        self.wavs = defaultdict(list)
        metadata = defaultdict(list)
        for name, (archive, info) in resources.index.items():
            parent = posixpath.dirname(name)
            while parent:
                self.directories.add(parent)
                parent = posixpath.dirname(parent)
            if name.endswith('.wav'):
                parent = posixpath.dirname(name)
                self.wavs[parent].append(name)
                metadata[parent].append({'archive': archive, 'member': name,
                                         'size': info.file_size, 'crc32': info.CRC})
        expected = {r['path']: r for r in catalog['asset_directories']}
        if set(expected) != set(metadata):
            raise ValueError('Sound asset-directory inventory changed')
        for folder, rows in metadata.items():
            rows.sort(key=lambda r: (r['archive'], r['member']))
            if (len(rows) != expected[folder]['immediate_wav_count'] or
                    digest(rows) != expected[folder]['central_directory_metadata_sha256']):
                raise ValueError(f'Sound metadata changed: {folder}')

    def lookup(self, request):
        path = member_path(request)
        if path in self.directories:
            folder, mode = path, 'directory'
            matches = self.wavs.get(folder, [])
        elif request.replace('\\', '/').endswith('/'):
            # An absent trailing-slash request is not treated as proof of the
            # mounted filesystem's file/directory classification.
            folder, mode, matches = path, 'absent_directory_request', []
        else:
            folder, mode = posixpath.dirname(path), 'specific_stem'
            stem = posixpath.splitext(posixpath.basename(path))[0]
            matches = [n for n in self.wavs.get(folder, [])
                       if posixpath.splitext(posixpath.basename(n))[0] == stem]
        result = {'path': request, 'normalized_path': path, 'selection': mode,
                  'matching_wav_count': len(matches),
                  'status': 'assets_found' if matches else 'no_matching_base_asset',
                  'selection_rng_draws': int(mode == 'directory' and len(matches) > 1)}
        if matches:
            definition = folder
            while definition not in self.folders and definition:
                definition = posixpath.dirname(definition)
            row = self.folders[definition]
            result.update({'effective_declaration': definition,
                           'signal': row['effective']['signal'],
                           'signal_period': row['effective']['signal_period'],
                           'signal_provenance': row['provenance'].get('signal')})
        return result


def compile_requests(markers, sound_index):
    macros = {h['name']: h['syntax'] for h in markers['handlers'] if h['kind'] == 'macro'}
    expander = PositionalExpressions(macros)
    handlers = [{**h, 'syntax': expander.expand(h['syntax'])[0]}
                for h in markers['handlers'] if h['kind'] != 'macro']
    events = {h['name']: h['syntax'] for h in handlers if h['kind'] == 'event'}
    aliases = defaultdict(list)

    def alias(node, origin):
        w = words(node)
        if len(w) != 3 or children(node):
            raise ValueError('Unreviewed link_sound shape')
        row = {'path': w[2], 'origin': origin, 'member': node['member'], 'line': node['line']}
        if row not in aliases[w[1]]:
            aliases[w[1]].append(row)

    for node in markers['spawn_sound_links']:
        alias(node, 'base_spawn')
    for handler in handlers:
        for node in walk(handler['syntax']):
            if words(node)[0] == 'link_sound':
                alias(node, 'marker_closure_declaration')
    requests = []

    def visit(node, marker, guards=(), containers=(), calls=()):
        w = words(node)
        if w[0] == 'if':
            branches, branch = [], []
            for item in node['items'][1:] + ['else']:
                if item == 'else':
                    condition, actions = [], []
                    for part in branch:
                        if isinstance(part, str):
                            if actions:
                                raise ValueError('Bare token after branch action')
                            condition.append(part)
                        else:
                            actions.append(part)
                    branches.append((condition, actions))
                    branch = []
                else:
                    branch.append(item)
            preceding = []
            arity = {'velocity': 1, 'lying': 0, 'terrain_fx': 1, 'rain': 0,
                     'season': 1, 'tagged': 1, 'altitude': 1}
            for branch_index, (condition, actions) in enumerate(branches):
                test = condition[1:] if condition[:1] == ['not'] else condition
                if condition:
                    if not test or test[0] not in arity or len(test) != arity[test[0]] + 1:
                        raise ValueError(f'Unreviewed marker condition: {condition}')
                elif branch_index == 0 or branch_index != len(branches) - 1:
                    raise ValueError('Unconditional branch must be the final else')
                checks = preceding + ([{'tokens': condition, 'result': True}] if condition else [])
                for action in actions:
                    visit(action, marker, guards + tuple(checks), containers, calls)
                if condition:
                    preceding = preceding + [{'tokens': condition, 'result': False}]
            return
        if w[0] in ('call', 'call_synced'):
            if len(w) != 2 or children(node) or w[1] not in events:
                raise ValueError('Unresolved named call')
            if w[1] in [r['event'] for r in calls]:
                raise ValueError('Cyclic named event call')
            visit(events[w[1]], marker, guards, containers,
                  calls + ({'event': w[1], 'mode': w[0], 'line': node['line']},))
            return
        if w[0] in ('start_sound', 'play_sound', 'stop_sound'):
            if children(node) or len(w) not in (2, 3):
                raise ValueError(f'Unreviewed sound request shape: {w}')
            loop = w[0] == 'play_sound' and len(w) == 3 and int(w[2]) != 0
            targets = ([{'path': w[1], 'origin': 'direct_request'}] if w[0] == 'start_sound'
                       else aliases.get(w[1], []))
            requests.append({'marker': marker, 'action': w[0], 'arguments': w[1:],
                             'loop': loop, 'member': node['member'], 'line': node['line'],
                             'macro_call_ids': node.get('macro_call_ids', []),
                             'guards': list(guards), 'containers': list(containers),
                             'named_calls': list(calls),
                             'alias_candidates_only': w[0] != 'start_sound',
                             'targets': [{**t, 'lookup': sound_index.lookup(t['path'])}
                                         for t in targets]})
            return
        next_containers = containers
        if children(node) and w[0] not in ('on',):
            next_containers += ({'action': w[0], 'arguments': w[1:], 'line': node['line']},)
        for child in children(node):
            visit(child, marker, guards, next_containers, calls)

    for handler in handlers:
        if handler['kind'] == 'marker':
            visit(handler['syntax'], handler['name'])
    paths = {t['path']: t['lookup'] for r in requests for t in r['targets']}
    return {'counts': {'marker_handlers': sum(h['kind'] == 'marker' for h in handlers),
                       'macro_invocations': len(expander.calls), 'sound_request_rows': len(requests),
                       'actions': dict(Counter(r['action'] for r in requests)),
                       'unique_literal_paths': len(paths),
                       'unique_normalized_paths': len({member_path(p) for p in paths}),
                       'missing_base_paths': sum(r['status'] != 'assets_found' for r in paths.values())},
            'macro_invocations': expander.calls, 'alias_declarations': dict(aliases),
            'requests': requests, 'unique_paths': [paths[p] for p in sorted(paths)],
            'aliases_without_closure_binding': sorted({r['arguments'][0] for r in requests
                                                        if r['alias_candidates_only'] and not r['targets']})}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--markers', type=Path, required=True)
    p.add_argument('--sounds', type=Path, required=True)
    p.add_argument('--resources-dir', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    markers = json.loads(args.markers.read_text())
    source = markers['resource']
    with zipfile.ZipFile(args.resources_dir / source['archive']) as z:
        data = z.read(source['member'])
        if hashlib.sha256(data).hexdigest() != source['sha256']:
            raise ValueError('Human marker resource changed')
    with ExitStack() as stack:
        archives = {'sound/' + name: stack.enter_context(zipfile.ZipFile(
            args.resources_dir / 'sound' / name)) for name in BASE_ARCHIVES}
        index = SoundIndex(Resources(archives), json.loads(args.sounds.read_text()))
        result = compile_requests(markers, index)
    result.update({'schema_version': 1, 'steam_build_id': '25492057', 'runtime_verified': False,
                   'scope': 'Potential requests in base marker/named-event closure, with ordered branch '
                            'guards and demand-resolved positional path expressions. Not branch reachability, '
                            'general SDL preprocessing, live alias ownership, mounted overrides or detection.',
                   'macro_limit': 'Unused forwarded arguments are recorded, not assigned invented values. '
                                  'Native treatment of omitted forwarded parameters remains unverified.',
                   'native_references': ['marker-actions-map.json', 'sound-definitions-map.json'],
                   'resource': source,
                   'validation': {'human_resource_sha256_matches': True,
                                  'all_base_sound_directory_metadata_matches': True,
                                  'emitted_sound_paths_have_no_unbound_positional_arguments': True},
                   'input_artifacts': [{'filename': path.name,
                                        'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                                       for path in (args.markers, args.sounds)]})
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result['counts'], indent=2))


if __name__ == '__main__':
    main()
