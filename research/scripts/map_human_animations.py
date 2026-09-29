#!/usr/bin/env python3
"""Catalog base human animation declarations without running the engine.

Supports the forms present in human_anm.ext and its two includes. Preserves
Windows path separators literally. Records declarations, not mounted resource
resolution, binary ANM timelines or native parser defaults not reviewed here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import posixpath
import re
import zipfile
from collections import Counter
from pathlib import Path

from map_human_fsm import Node, atoms, children, head, shape


TOKEN = re.compile(r'\s+|;[^\r\n]*|"[^"\r\n]*"|[{}()]|[^\s{}();"]+')


def parse(text: str, source: str) -> list[Node]:
    roots, stack = [], []
    position = 0
    for match in TOKEN.finditer(text):
        if match.start() != position:
            raise ValueError(f"Unsupported token in {source} at {position}")
        position = match.end()
        value = match[0]
        if value.isspace() or value.startswith(';'):
            continue
        if value in ('{', '('):
            node = Node(value, [], source, text.count('\n', 0, match.start()) + 1)
            (stack[-1].items if stack else roots).append(node)
            stack.append(node)
        elif value in ('}', ')'):
            if not stack or stack[-1].delimiter != {'}': '{', ')': '('}[value]:
                raise ValueError(f"Unmatched delimiter in {source}")
            stack.pop()
        else:
            if not stack:
                raise ValueError(f"Bare top-level token in {source}")
            # SDL animation paths use literal backslashes, not C/Python escapes.
            if value.startswith('"'):
                value = value[1:-1]
                if value.endswith('\\'):
                    raise ValueError("Ambiguous trailing backslash in quoted token")
            stack[-1].items.append(value)
    if position != len(text) or stack:
        raise ValueError(f"Incomplete SDL in {source}")
    return roots


class Reader:
    def __init__(self, archive: zipfile.ZipFile):
        self.archive = archive
        self.resources: dict[str, dict] = {}

    def read(self, member: str, ancestry: tuple = ()) -> list[Node]:
        member = posixpath.normpath(member)
        if member in ancestry:
            raise ValueError(f"Cyclic include: {member}")
        data = self.archive.read(member)
        if b'\0' in data:
            raise ValueError(f"Not a text resource: {member}")
        self.resources[member] = {
            'archive': Path(self.archive.filename).name, 'member': member,
            'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
        }
        return self.expand(parse(data.decode('cp1251'), member), ancestry + (member,))

    def expand(self, nodes: list[Node], ancestry: tuple) -> list[Node]:
        result = []
        for node in nodes:
            if node.delimiter == '(':
                if head(node) != 'include':
                    raise ValueError(f"Unreviewed macro: {atoms(node)}")
                shape(node, (2,))
                member = posixpath.join(posixpath.dirname(node.source), atoms(node)[1])
                result.extend(self.read(member, ancestry))
            else:
                items = []
                for item in node.items:
                    items.extend(self.expand([item], ancestry)
                                 if isinstance(item, Node) else [item])
                result.append(Node(node.delimiter, items, node.source, node.line))
        return result


def compile_catalog(nodes: list[Node]) -> dict:
    if len(nodes) != 1 or head(nodes[0]) != 'animation':
        raise ValueError('Expected one animation root')
    shape(nodes[0], (1,), allow_children=True)
    smooth, definitions = None, []
    scalar = {'smooth': (2,), 'speed': (2,), 'file': (2,), 'pose': (3,),
              'pinback': (1, 2), 'rotate': (6,)}
    flags = {'resume', 'movement', 'additive', 'autostart', 'store'}
    for node in children(nodes[0]):
        kind, words = head(node), atoms(node)
        if kind == 'smooth':
            shape(node, (2,))
            smooth = float(words[1])
            continue
        if kind not in ('sequence', 'blend'):
            raise ValueError(f"Unreviewed animation type: {kind}")
        shape(node, (2,), allow_children=True)
        record = {'index': len(definitions), 'name': words[1], 'type': kind,
                  'member': node.source, 'line': node.line,
                  'inherited_smooth': smooth, 'properties': [], 'events': [],
                  'dynamic_speed_declarations': []}
        for prop in children(node):
            key, values = head(prop), atoms(prop)[1:]
            if key in scalar:
                shape(prop, scalar[key])
                if key == 'pose' and values[1] not in ('begin', 'end'):
                    raise ValueError(f"Unsupported pose endpoint: {values}")
            elif key in flags:
                shape(prop, (1,))
            elif key in ('events', 'dynamicspeed'):
                shape(prop, (1,), allow_children=True)
                for row in children(prop):
                    shape(row, (2,) if key == 'events' else (3,))
                    fields = atoms(row)
                    if key == 'events':
                        record['events'].append({'frame': int(fields[0]),
                                                 'name': fields[1], 'line': row.line})
                    else:
                        record['dynamic_speed_declarations'].append({
                            'start': float(fields[0]), 'end': float(fields[1]),
                            'factor': float(fields[2]), 'line': row.line})
            else:
                raise ValueError(f"Unreviewed animation property: {key}")
            record['properties'].append({'name': key, 'values': values,
                                         'line': prop.line})
        definitions.append(record)
    counts = Counter(r['name'].lower() for r in definitions)
    events = Counter(e['name'] for r in definitions for e in r['events'])
    return {
        'schema_version': 1, 'runtime_verified': False,
        'scope': 'Ordered base declarations only; no mounted overrides, binary file '
                 'resolution, timeline normalization or action execution modeled.',
        'counts': {'definitions': len(definitions),
                   'types': dict(Counter(r['type'] for r in definitions)),
                   'unique_names_casefolded': len(counts),
                   'event_declarations': sum(events.values()),
                   'event_names': dict(sorted(events.items()))},
        'duplicate_names_casefolded': {n: c for n, c in counts.items() if c > 1},
        'definitions': definitions,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--entry', default='properties/animation/human/human_anm.ext')
    parser.add_argument('--native-map', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with zipfile.ZipFile(args.archive) as archive:
        reader = Reader(archive)
        catalog = compile_catalog(reader.read(args.entry))
        catalog['resources'] = list(reader.resources.values())
    if args.native_map:
        native = json.loads(args.native_map.read_text())
        catalog['native_map_sha256'] = hashlib.sha256(args.native_map.read_bytes()).hexdigest()
        catalog['steam_build_id'] = native['steam_build_id']
        catalog['native_reference_join'] = []
        for ref in native['native_animation_references']:
            matches = [r['index'] for r in catalog['definitions']
                       if r['name'].lower() == ref['name'].lower()]
            catalog['native_reference_join'].append({**ref, 'definition_indices': matches})
    args.output.write_text(json.dumps(catalog, indent=2) + '\n')
    print(json.dumps(catalog['counts'], indent=2))


if __name__ == '__main__':
    main()
