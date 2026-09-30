#!/usr/bin/env python3
"""Expand the base human select_idle declaration into an ordered decision tree.

This is a bounded static extractor, not an engine or a probability simulator.
Supports the two local named-argument macros and the native conditions reviewed
in idle-actions-map. Empty branches are preserved because they stop an else chain.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import zipfile
from collections import Counter
from pathlib import Path

from map_human_fsm import Node, atoms, children, head, parse, shape


class Expander:
    def __init__(self):
        self.macros = {}
        self.invocations = []

    def expand(self, nodes: list[Node], depth: int = 0) -> list[Node]:
        if depth > 30:
            raise ValueError('Macro recursion limit')
        result = []
        for node in nodes:
            name = head(node)
            if node.delimiter == '(' and name == 'define':
                shape(node, (2,), allow_children=True)
                if atoms(node)[1] in self.macros:
                    raise ValueError('Repeated macro')
                self.macros[atoms(node)[1]] = children(node)
            elif node.delimiter == '(':
                if name not in self.macros:
                    raise ValueError(f'Unknown macro {name}')
                args = {}
                tail = node.items[1:]
                if len(tail) % 2:
                    raise ValueError('Expected named argument/value pairs')
                for index in range(0, len(tail), 2):
                    key, value = tail[index:index + 2]
                    if not isinstance(key, str) or not isinstance(value, Node):
                        raise ValueError('Malformed named argument')
                    if value.delimiter != '(' or len(value.items) != 1 or children(value):
                        raise ValueError('Expected one scalar argument value')
                    if key in args:
                        raise ValueError('Duplicate named argument')
                    args[key] = value.items[0]
                self.invocations.append({'name': name, 'arguments': args,
                                         'member': node.source, 'line': node.line})

                def substitute(value):
                    if isinstance(value, Node):
                        return Node(value.delimiter, [substitute(x) for x in value.items],
                                    value.source, value.line)
                    if value.startswith('%'):
                        if value[1:] not in args:
                            raise ValueError(f'Missing macro argument {value}')
                        return args[value[1:]]
                    return value
                result.extend(self.expand([substitute(x) for x in self.macros[name]], depth + 1))
            else:
                parts = []
                for item in node.items:
                    parts.extend(self.expand([item], depth + 1)
                                 if isinstance(item, Node) else [item])
                result.append(Node(node.delimiter, parts, node.source, node.line))
        return result


def compile_policy(nodes: list[Node]) -> dict:
    if len(nodes) != 1 or atoms(nodes[0]) != ['on', 'select_idle']:
        raise ValueError('Expected one select_idle event')
    conditions, choices = Counter(), []
    zero_args = {'no_stuff', 'in_cover', 'standing', 'squatting', 'lying',
                 'swimming', 'idle_low_health', 'hold_strap'}

    def action(node):
        words = atoms(node)
        common = {'member': node.source, 'line': node.line}
        if head(node) == 'set_idle_animation':
            if children(node):
                raise ValueError('Nested animation choice')
            choices.extend(words[1:])
            return {**common, 'action': 'set_idle_animation', 'names': words[1:]}
        if head(node) != 'if':
            raise ValueError(f'Unsupported action {head(node)}')
        branches, parts = [], []
        for item in node.items[1:] + ['else']:
            if item == 'else':
                cond, actions, started = [], [], False
                for part in parts:
                    if isinstance(part, Node):
                        started = True
                        actions.append(action(part))
                    elif started:
                        raise ValueError('Bare token after branch actions')
                    else:
                        cond.append(part)
                negate = bool(cond and cond[0] == 'not')
                if negate:
                    cond = cond[1:]
                key = cond[0] if cond else None
                args = cond[1:]
                if key in zero_args:
                    if args:
                        raise ValueError(f'Unexpected arguments for {key}')
                elif key == 'rand':
                    if len(args) != 1:
                        raise ValueError('Random condition needs one float')
                    float(args[0])
                elif key in ('stuff', 'cover_type'):
                    if not args:
                        raise ValueError('Missing descriptor')
                elif key is not None:
                    raise ValueError(f'Unsupported condition {key}')
                conditions[key or 'else'] += 1
                branches.append({'condition': key, 'negated': negate,
                                 'arguments': args, 'actions': actions})
                parts = []
            else:
                parts.append(item)
        return {**common, 'action': 'if', 'branches': branches}

    actions = [action(n) for n in children(nodes[0])]
    return {'actions': actions, 'condition_counts': dict(sorted(conditions.items())),
            'animation_names': sorted(set(choices))}


def extract(text: str, member: str) -> dict:
    expander = Expander()
    result = compile_policy(expander.expand(parse(text, member)))
    result['macros'] = sorted(expander.macros)
    result['macro_invocations'] = expander.invocations
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    member = 'set/interaction_entity/human_idle.inc'
    with zipfile.ZipFile(args.archive) as archive:
        data = archive.read(member)
    result = extract(data.decode('cp1251'), member)
    catalog = json.loads(args.catalog.read_text())
    result.update({'schema_version': 1, 'steam_build_id': '25492057',
                   'runtime_verified': False,
                   'scope': 'Base script decision tree; no mounted overrides, execution '
                            'or statistical independence of RNG draws assumed.',
                   'resource': {'archive': args.archive.name, 'member': member,
                                'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()},
                   'animation_catalog_sha256': hashlib.sha256(args.catalog.read_bytes()).hexdigest()})
    result['animation_join'] = [
        {'name': n, 'definition_indices': [d['index'] for d in catalog['definitions']
                                          if d['name'].lower() == n.lower()]}
        for n in result['animation_names']]
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'macros': len(result['macros']),
                      'macro_invocations': len(result['macro_invocations']),
                      'conditions': result['condition_counts'],
                      'animation_names': len(result['animation_names']),
                      'unmatched': [r['name'] for r in result['animation_join']
                                    if not r['definition_indices']]}, indent=2))


if __name__ == '__main__':
    main()
