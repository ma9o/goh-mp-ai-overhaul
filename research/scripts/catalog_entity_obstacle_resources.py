#!/usr/bin/env python3
"""Bounded census of authored automatic-obstacle options, without mount resolution."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import zipfile
from collections import Counter
from pathlib import Path

from catalog_bounding_shapes import ARCHIVES, EXTENSIONS
from map_human_fsm import atoms, children, head, parse

TERMS = ('compoundobstacles', 'obstacle_ignore_height')
PATTERN = re.compile(rb'\b(?:compoundobstacles|obstacle_ignore_height)\b', re.I)
TEXT_PATTERN = re.compile(r'\b(?:compoundobstacles|obstacle_ignore_height)\b', re.I)


def walk(nodes, parents=()):
    for node in nodes:
        own = atoms(node)
        matched = Counter(m.group().lower() for atom in own
                          for m in TEXT_PATTERN.finditer(atom))
        if matched:
            yield {'line': node.line, 'head': head(node), 'delimiter': node.delimiter,
                   'parent_path': list(parents), 'terms': dict(sorted(matched.items())),
                   'compound_obstacles_braced_head':
                       node.delimiter == '{' and head(node) == 'compoundobstacles'}
        yield from walk(children(node), parents + (head(node),))


def catalog(root):
    result = {'schema_version': 1, 'steam_build_id': '25492057', 'runtime_verified': False,
              'scope': '13 base archives and11 text extensions, same corpus as bounding-shape census. '
                       'Literal matches include comments. Parsed nodes exclude comments; own atoms '
                       'are searched without expanding macros/includes or resolving mounts. A tag '
                       'mention does not prove attachment to a live entity. On full-parse failure, '
                       'complete single-line braced declarations are parsed separately with unknown '
                       'parent context and reported as recovered fragments. No maps, mods or binaries.',
              'archives': [], 'matching_members': [], 'parsed_nodes': [], 'parse_failures': [],
              'recovered_fragments': []}
    totals = Counter()
    for name in ARCHIVES:
        count = size = 0
        literals = Counter()
        digest = hashlib.sha256()
        with zipfile.ZipFile(root / name) as archive:
            entries = sorted((i for i in archive.infolist()
                              if Path(i.filename).suffix.lower() in EXTENSIONS),
                             key=lambda i: i.filename)
            if len({i.filename for i in entries}) != len(entries):
                raise ValueError(f'Duplicate member names in {name}')
            for info in entries:
                data = archive.read(info)
                checksum = hashlib.sha256(data).hexdigest()
                digest.update(json.dumps([info.filename, len(data), checksum],
                                         ensure_ascii=True, separators=(',', ':')).encode() + b'\n')
                count += 1
                size += len(data)
                matched = Counter(m.group().decode().lower() for m in PATTERN.finditer(data))
                if not matched:
                    continue
                literals.update(matched)
                row = {'archive': name, 'member': info.filename, 'size': len(data),
                       'sha256': checksum, 'literal_counts': dict(sorted(matched.items()))}
                result['matching_members'].append(row)
                try:
                    if b'\0' in data:
                        raise ValueError('NUL in text candidate')
                    records = list(walk(parse(data.decode('cp1251'), info.filename)))
                except (ValueError, UnicodeError) as error:
                    row['parsed'] = False
                    result['parse_failures'].append({'archive': name, 'member': info.filename,
                                                     'reason': str(error)})
                    for line_number, line in enumerate(data.decode('cp1251').splitlines(), 1):
                        if not line.lstrip().startswith('{') or not TEXT_PATTERN.search(line):
                            continue
                        try:
                            fragments = list(walk(parse(line, info.filename)))
                        except ValueError:
                            continue
                        for fragment in fragments:
                            fragment['line'] = line_number
                            fragment['parent_path'] = None
                            result['recovered_fragments'].append({
                                'archive': name, 'member': info.filename,
                                'scope': 'Complete single-line declaration; full file parse failed',
                                **fragment})
                    continue
                row['parsed'] = True
                for record in records:
                    totals.update(record['terms'])
                    result['parsed_nodes'].append({'archive': name, 'member': info.filename, **record})
        result['archives'].append({'archive': name, 'extensions': sorted(EXTENSIONS),
                                   'members_scanned': count, 'bytes_scanned': size,
                                   'scanned_member_digest_sha256': digest.hexdigest(),
                                   'digest_format': 'Sorted UTF-8 JSON [name,size,sha256] plus LF per scanned member',
                                   'literal_counts': dict(sorted(literals.items()))})
    result['parsed_term_counts'] = dict(sorted(totals.items()))
    result['compound_obstacles_braced_heads'] = sum(
        n['compound_obstacles_braced_head'] for n in result['parsed_nodes'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = catalog(args.resources_dir)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'members_scanned': sum(x['members_scanned'] for x in result['archives']),
                      'matching_members': len(result['matching_members']),
                      'parse_failures': len(result['parse_failures']),
                      'recovered_fragments': len(result['recovered_fragments']),
                      'parsed_term_counts': result['parsed_term_counts'],
                      'compound_obstacles_braced_heads': result['compound_obstacles_braced_heads']}, indent=2))


if __name__ == '__main__':
    main()
