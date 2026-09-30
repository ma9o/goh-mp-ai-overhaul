#!/usr/bin/env python3
"""Check additive decision-catalog passes without changing prior artifacts.

Checks file hashes, entries/instruction membership, contract-column presence,
row links and declared bounded access census. This does not prove meanings,
lifetime, ABI or completeness beyond the declared function scope.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
from verify_fine_map import require, verify

def verify_pass(path: Path, binaries: Path, exports: Path) -> dict:
    result = verify(path, binaries, exports)
    manifest = json.loads(path.read_text())
    rows = manifest['fields'] + manifest['outputs'] + manifest['float_constants']
    ids = [r['id'] for r in rows]
    require(len(ids) == len(set(ids)), 'Duplicate row ID')
    evidence = {e['id']: e for e in manifest['evidence']}
    for group, columns in [
        (manifest['fields'], {'read_site', 'access_path', 'meaning', 'freshness', 'validity'}),
        (manifest['outputs'], {'effect', 'result_codes', 'ownership', 'cancellation', 'side_effects'}),
    ]:
        for row in group:
            require(set(row['contract']) == columns, f'Wrong columns: {row["id"]}')
            require(all(isinstance(v, str) and v.strip() for v in row['contract'].values()),
                    f'Implicit blank: {row["id"]}')
            require(bool(row['sites']), f'No sites: {row["id"]}')
            available = {s for eid in row['evidence_ids'] for s in evidence[eid]['instruction_rvas']}
            require(set(row['sites']) <= available, f'Unlinked sites: {row["id"]}')
    for name, digest in manifest.get('source_maps', {}).items():
        require(hashlib.sha256((path.parent / (name + '-map.json')).read_bytes()).hexdigest() == digest,
                f'Changed source map: {name}')
    directory = exports / ('call_to_arms' + manifest['export_suffix'])
    expected = set()
    for function in manifest['scope_functions']:
        for line in (directory / (function + '.asm')).read_text().splitlines():
            if not line.strip():
                continue
            address, instruction = line.split('\t', 1)
            if '[' in instruction or instruction.startswith('CALL '):
                expected.add((function, address))
    records = manifest['access_audit']
    actual = [(r['function_rva'], r['instruction_rva']) for r in records]
    require(len(actual) == len(set(actual)), 'Duplicate audit site')
    require(set(actual) == expected, f'Access census mismatch: missing={expected-set(actual)}; extra={set(actual)-expected}')
    rows_by_id = {r['id']: r for r in rows}
    for record in records:
        require(bool(record['row_ids']) != bool(record['exclusion']), 'Need rows OR explicit exclusion')
        for id in record['row_ids']:
            require(id in rows_by_id, f'Unknown row: {id}')
            require(record['instruction_rva'] in rows_by_id[id]['sites'], f'Row/site mismatch: {id}')
    result['decision_pass'] = dict(coverage_row=manifest['coverage_row'],
        input_rows=len(manifest['fields']), output_rows=len(manifest['outputs']),
        scope_functions=len(manifest['scope_functions']), classified_sites=len(records),
        excluded_sites=sum(bool(r['exclusion']) for r in records),
        source_hashes=len(manifest.get('source_maps', {})), claim_limit=manifest['claim_limit'])
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--map', type=Path, required=True)
    parser.add_argument('--binaries-dir', type=Path, required=True)
    parser.add_argument('--exports-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        result = verify_pass(args.map, args.binaries_dir, args.exports_dir)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f'Decision pass verification failed: {error}\n')
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')

if __name__ == '__main__':
    main()
