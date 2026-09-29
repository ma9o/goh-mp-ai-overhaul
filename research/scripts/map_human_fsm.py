#!/usr/bin/env python3
"""Extract the installed human FSM declarations and model isolated request paths.

This intentionally supports only the SDL forms used by human_fsm.ext and its
includes, rejecting other forms. It never runs game code. Parser ordering,
subset matching and BFS follow the contracts reviewed in fsm-routing-map.
It does not model request arbitration, animation callbacks or actual playability.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import posixpath
import re
import zipfile
from collections import deque
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Node:
    delimiter: str
    items: list
    source: str
    line: int


TOKEN = re.compile(r'\s+|;[^\r\n]*|"(?:\\.|[^"\\])*"|[{}()]|[^\s{}();"]+')


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
                raise ValueError(f"Bare top-level token in {source}: {value}")
            # No escape syntax is needed by these resources; reject rather than
            # silently implementing Python/C escaping in place of native SDL.
            if value.startswith('"'):
                value = value[1:-1]
                if '\\' in value:
                    raise ValueError(f"Unreviewed string escape in {source}")
            stack[-1].items.append(value)
    if position != len(text) or stack:
        raise ValueError(f"Incomplete SDL in {source}")
    return roots


def head(node: Node) -> str:
    if not node.items or not isinstance(node.items[0], str):
        raise ValueError(f"Missing directive name at {node.source}:{node.line}")
    return node.items[0].lower()


def atoms(node: Node) -> list[str]:
    return [value for value in node.items if isinstance(value, str)]


def children(node: Node) -> list[Node]:
    return [value for value in node.items if isinstance(value, Node)]


def shape(node: Node, counts: tuple[int, ...], allow_children: bool = False) -> None:
    if len(atoms(node)) not in counts or (children(node) and not allow_children):
        raise ValueError(f"Unsupported {head(node)} form at {node.source}:{node.line}")


class Expander:
    def __init__(self, archive: zipfile.ZipFile):
        self.archive = archive
        self.macros: dict[str, list[Node]] = {}
        self.resources: dict[str, dict] = {}
        self.invocations = 0
        self.expansions: list[dict] = []

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
        # Names/directives are ASCII; CP1251 also preserves Russian comments.
        return self.expand(parse(data.decode('cp1251'), member), ancestry + (member,))

    def expand(self, nodes: list[Node], ancestry: tuple) -> list[Node]:
        result = []
        for node in nodes:
            name, words = head(node), atoms(node)
            if node.delimiter == '{':
                expanded = copy.deepcopy(node)
                # Children may contain include/macro directives, e.g. FSM body.
                parts = []
                for value in expanded.items:
                    parts.extend(self.expand([value], ancestry)
                                 if isinstance(value, Node) else [value])
                expanded.items = parts
                result.append(expanded)
            elif name == 'include':
                if len(words) != 2 or children(node):
                    raise ValueError('Unsupported include form')
                member = posixpath.join(posixpath.dirname(node.source), words[1])
                result.extend(self.read(member, ancestry))
            elif name == 'define':
                if len(words) != 2:
                    raise ValueError('Unsupported define form')
                if words[1].lower() in self.macros:
                    raise ValueError(f"Repeated macro: {words[1]}")
                self.macros[words[1].lower()] = children(node)
            else:
                if (name not in self.macros or len(words) < 2 or words[1] != 'args'
                    or children(node)):
                    raise ValueError(f"Unsupported macro invocation: {words}")
                if len(ancestry) > 40:
                    raise ValueError('Macro/include nesting limit exceeded')
                arguments = words[2:]
                self.invocations += 1
                self.expansions.append({'name': name, 'arguments': arguments,
                                        'member': node.source, 'line': node.line})

                def substitute(value):
                    if isinstance(value, Node):
                        return Node(value.delimiter, [substitute(x) for x in value.items],
                                    value.source, value.line)

                    def replace(match):
                        index = int(match[1])
                        if index >= len(arguments):
                            raise ValueError(f"Missing argument {index} for {name}")
                        return arguments[index]
                    return re.sub(r'%([0-9]+)', replace, value)

                result.extend(self.expand([substitute(x) for x in self.macros[name]],
                                          ancestry + ('macro:' + name,)))
        return result


def compile_graph(nodes: list[Node]) -> dict:
    if len(nodes) != 1 or head(nodes[0]) != 'fsm':
        raise ValueError('Expected one FSM root')
    shape(nodes[0], (2,), allow_children=True)
    states, requests, subsets, switches = {}, [], {}, {}
    split_count, defaults, start = 0, [], None

    def state(name: str) -> dict:
        if name not in states:
            states[name] = {'index': len(states), 'name': name, 'declared': False,
                            'neighbors': [], 'transit': None, 'tags': [],
                            'parts': defaults.copy(), 'conditions': [], 'sources': []}
        return states[name]

    def switch(name: str) -> dict:
        if name not in switches:
            switches[name] = {'index': len(switches), 'name': name, 'pairs': []}
        return switches[name]

    for node in children(nodes[0]):
        key, words = head(node), atoms(node)
        if key == 'split':
            shape(node, (2,))
            split_count = int(words[1])
            if split_count < 0:
                raise ValueError('Negative split count is not modeled')
            defaults.extend(['linked'] * split_count)
        elif key == 'start':
            shape(node, (2,))
            start = words[1]
            state(start)
        elif key == 'state':
            shape(node, (2,), allow_children=True)
            current = state(words[1])
            current['declared'] = True
            current['sources'].append({'member': node.source, 'line': node.line})
            for directive in children(node):
                kind, args = head(directive), atoms(directive)[1:]
                if children(directive):
                    raise ValueError('Nested state directive is not modeled')
                if kind in ('tag', 'condition'):
                    if len(args) != 1:
                        raise ValueError(f"Unexpected {kind} arity")
                    current['tags' if kind == 'tag' else 'conditions'].append(args[0])
                elif kind in ('to', 'transit', 'from'):
                    if len(args) != 1:
                        raise ValueError(f"Unexpected {kind} arity")
                    other = state(args[0])
                    if kind == 'from':
                        other['neighbors'].append(current['name'])
                    else:
                        current['neighbors'].append(other['name'])
                        if kind == 'transit':
                            current['transit'] = other['name']
                elif kind == 'fromtagged':
                    # Native parser evaluates existing tags at this exact point.
                    for other in states.values():
                        if all(tag in other['tags'] for tag in args):
                            other['neighbors'].append(current['name'])
                elif kind == 'split':
                    if split_count < 1 or len(args) != split_count:
                        raise ValueError('Invalid state split count')
                    current['parts'] = args
                else:
                    raise ValueError(f"Unsupported state directive: {kind}")
        elif key == 'request':
            shape(node, (2,), allow_children=True)
            request = {'name': words[1], 'priority': 0, 'duration': 0,
                       'interrupt': False, 'exit': '', 'switch': None, 'reverse': False}
            for directive in children(node):
                kind, args = head(directive), atoms(directive)[1:]
                if kind in ('priority', 'duration'):
                    shape(directive, (2,))
                    request[kind] = int(args[0])
                elif kind == 'exit':
                    shape(directive, (2,))
                    request[kind] = args[0]
                elif kind == 'interrupt':
                    shape(directive, (1,))
                    request[kind] = True
                elif kind == 'switch':
                    shape(directive, (3,))
                    switch(args[0])
                    request['switch'], request['reverse'] = args[0], int(args[1]) == 0
                else:
                    raise ValueError(f"Unsupported request directive: {kind}")
            requests.append(request)
        elif key == 'subset':
            shape(node, (2, 4), allow_children=True)
            rules = []
            if len(words) > 2:
                if len(words) != 4 or words[2].lower() != 'from' or words[3] not in subsets:
                    raise ValueError('Unsupported/unknown subset inheritance')
                rules = copy.deepcopy(subsets[words[3]]['rules'])
            for directive in children(node):
                kind, args = head(directive), atoms(directive)[1:]
                if kind not in ('enable', 'disable') or children(directive):
                    raise ValueError(f"Unsupported subset rule: {kind}")
                # Installed masks here are positive names only. Do not pretend
                # to implement the native general tag-expression parser.
                if any(re.search(r'[^A-Za-z0-9_-]', tag) for tag in args):
                    raise ValueError('Unreviewed subset mask expression')
                rules.append({'enabled': kind == 'enable', 'all_tags': args})
            if words[1] in subsets:
                raise ValueError('Duplicate subset not modeled')
            subsets[words[1]] = {'name': words[1], 'rules': rules}
        elif key == 'switch':
            shape(node, (2,), allow_children=True)
            current = switch(words[1])
            for directive in children(node):
                kind, args = head(directive), atoms(directive)[1:]
                if kind == 'link':
                    shape(directive, (3,))
                    state(args[0]); state(args[1])
                    current['pairs'].append(args)
                elif kind == 'split':
                    shape(directive, (4,))
                    index = int(args[0])
                    if not 0 <= index < split_count:
                        raise ValueError('Invalid switch split index')
                    for source in states.values():
                        if source['parts'][index] == args[1]:
                            for target in states.values():
                                if (target['parts'][index] == args[2] and
                                    all(a == b for i, (a, b) in enumerate(
                                        zip(source['parts'], target['parts'])) if i != index)):
                                    current['pairs'].append([source['name'], target['name']])
                                    break
                else:
                    raise ValueError(f"Unsupported switch directive: {kind}")
        else:
            raise ValueError(f"Unsupported FSM directive: {key}")
    if start not in states or any(not s['declared'] for s in states.values()):
        raise ValueError('Missing start or undeclared state references')
    return {'handler': atoms(nodes[0])[1], 'start': start, 'split_count': split_count,
            'states': list(states.values()), 'requests': requests,
            'subsets': list(subsets.values()), 'switches': list(switches.values())}


def enabled_states(graph: dict, subset: str | None) -> dict[str, bool]:
    rules = [] if subset is None else next(s['rules'] for s in graph['subsets']
                                           if s['name'] == subset)
    result = {}
    for state in graph['states']:
        enabled = True
        for rule in rules:
            if all(tag in state['tags'] for tag in rule['all_tags']):
                enabled = rule['enabled']
        result[state['name']] = enabled
    return result


def route(graph: dict, start: str, request: str, enabled: dict[str, bool]) -> list[str] | None:
    """Chronological route excluding start, or None; empty means already tagged."""
    states = {s['name']: s for s in graph['states']}
    queue, previous = deque([start]), {start: None}
    while queue:
        name = queue.popleft()
        current = states[name]
        if request in current['tags']:
            path = []
            while previous[name] is not None:
                path.append(name)
                name = previous[name]
            return path[::-1]
        for neighbor in reversed(current['neighbors']):
            target = states[neighbor]
            if (neighbor not in previous and enabled[neighbor] and
                (not target['conditions'] or request in target['conditions'])):
                previous[neighbor] = name
                queue.append(neighbor)
    return None


def analyze(archive_path: Path, build_id: str) -> dict:
    with zipfile.ZipFile(archive_path) as archive:
        expander = Expander(archive)
        graph = compile_graph(expander.read('properties/human_fsm.ext'))
    summaries = []
    states = graph['states']
    for subset in graph['subsets']:
        enabled = enabled_states(graph, subset['name'])
        request_rows = []
        for request in graph['requests']:
            paths = {s['name']: route(graph, s['name'], request['name'], enabled) for s in states}
            request_rows.append({
                'name': request['name'],
                'reachable_from_state_count': sum(p is not None for p in paths.values()),
                'already_tagged_source_count': sum(p == [] for p in paths.values()),
                'nonempty_route_source_count': sum(bool(p) for p in paths.values()),
                'reachable_from_enabled_source_count': sum(p is not None and enabled[n]
                                                           for n, p in paths.items()),
                'paths_from_named_states': {name: paths[name]
                                           for name in ('stand', 'lie', 'squat', 'walk', 'snipe')
                                           if name in paths},
            })
        summaries.append({
            'subset': subset['name'], 'enabled_states': sum(enabled.values()),
            'disabled_states': [name for name, allowed in enabled.items() if not allowed],
            'requests': request_rows,
        })
    # Independent structural expectations recovered from native parser case -2.
    # Unlike request BFS, this reachability ignores all runtime filtering.
    by_name = {s['name']: s for s in states}
    seen, queue = {graph['start']}, deque([graph['start']])
    while queue:
        for neighbor in by_name[queue.popleft()]['neighbors']:
            if neighbor not in seen:
                seen.add(neighbor)
                queue.append(neighbor)
    diagnostics = []
    for state in states:
        name = state['name']
        if name not in ('dead', 'common'):
            if not state['neighbors']:
                diagnostics.append({'state': name, 'issue': 'no_exit'})
            else:
                count = sum('dying' in by_name[n]['tags'] for n in state['neighbors'])
                if count != 1:
                    diagnostics.append({'state': name, 'issue': 'dying_neighbor_count', 'count': count})
        if name != 'boarding' and name not in seen:
            diagnostics.append({'state': name, 'issue': 'no_bare_path_from_start'})
    return {
        'schema_version': 1, 'steam_build_id': build_id, 'runtime_verified': False,
        'scope': 'Installed base human FSM declaration graph and isolated BFS/subset model.',
        'limitations': [
            'Preprocessor subset reconstructed from installed include/define/args forms; not the full SDL implementation.',
            'Does not include mods, mounted-archive overrides, arbitration, protected requests, owner tokens or animation timing.',
            'Source state is not required enabled, matching native search; its existing tag returns an empty path.',
            'Paths exclude their source and are chronological; native storage is reversed.',
            'A path proves only graph reachability under the modeled subset and request condition.',
        ],
        'resource_members': list(expander.resources.values()),
        'macro_definitions': sorted(expander.macros), 'macro_invocations': expander.invocations,
        'macro_expansions': expander.expansions,
        'counts': {'states': len(states), 'requests': len(graph['requests']),
                   'subsets': len(graph['subsets']), 'switches': len(graph['switches']),
                   'edges_including_duplicates': sum(len(s['neighbors']) for s in states),
                   'unique_directed_edges': len({(s['name'], n) for s in states for n in s['neighbors']})},
        'native_parser_structural_diagnostics': diagnostics,
        'bare_graph_unreachable_from_start': [s['name'] for s in states if s['name'] not in seen],
        'declared_requests_without_matching_state_tag': [r['name'] for r in graph['requests']
                                                        if all(r['name'] not in s['tags'] for s in states)],
        'native_contract_reference': 'fsm-routing-map.json', 'graph': graph,
        'isolated_request_reachability': summaries,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--build-id', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = analyze(args.archive, args.build_id)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'counts': report['counts'], 'resources': len(report['resource_members']),
                      'macro_invocations': report['macro_invocations']}))


if __name__ == '__main__':
    main()
