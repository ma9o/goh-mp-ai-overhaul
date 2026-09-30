#!/usr/bin/env python3
"""Verify Q1 catalog links and its bounded selection-access inventory.

Delegates PE/export/RTTI/constant/instruction checks to verify_fine_map.py.
The additional census checks classification coverage, not the truth of the
classifications, semantic completeness, lifetime, or a callable ABI.
Reads files only; never loads or modifies the game executable.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from verify_fine_map import require, verify


def verify_catalog(manifest_path: Path, binaries: Path, exports: Path) -> dict:
    result = verify(manifest_path, binaries, exports)
    manifest = json.loads(manifest_path.read_text())
    inputs, outputs = manifest['fields'], manifest['outputs']
    rows = inputs + outputs + manifest['float_constants'] + manifest['coverage']
    row_ids = [row['id'] for row in rows]
    require(len(row_ids) == len(set(row_ids)), 'Duplicate catalog row ID')
    evidence_ids = {e['id'] for e in manifest['evidence']}
    for group, columns in [
        (inputs, {'read_site', 'access_path', 'meaning', 'freshness', 'validity'}),
        (outputs, {'effect', 'result_codes', 'ownership', 'cancellation', 'side_effects'}),
    ]:
        for row in group:
            require(set(row['contract']) == columns, f'Incomplete contract columns: {row["id"]}')
            require(all(isinstance(v, str) and v.strip() for v in row['contract'].values()),
                    f'Empty implicit blank: {row["id"]}')
            require(set(row['evidence_ids']) <= evidence_ids, f'Unlinked evidence: {row["id"]}')
            validity = row['contract'].get('validity', '')
            if validity.startswith('V-'):
                require(validity in manifest['validity_contracts'], f'Unknown validity contract: {validity}')

    inherited = {}
    for name, source in manifest['source_maps'].items():
        path = manifest_path.parent / source['map']
        require(hashlib.sha256(path.read_bytes()).hexdigest() == source['sha256'],
                f'Changed source map: {name}')
        inherited[path.name] = json.loads(path.read_text())
    for e in manifest['evidence']:
        if 'inherited_from' in e:
            source = e['inherited_from']
            candidates = [old for old in inherited[source['map']]['evidence']
                          if old['id'] == source['evidence_id'] and old['binary'] == 'game']
            require(len(candidates) == 1, f'Missing inherited evidence: {e["id"]}')
            require(all(e[key] == candidates[0][key]
                        for key in ['function_rva', 'instruction_rvas']),
                    f'Altered inherited instruction locations: {e["id"]}')

    audit = manifest['selection_access_audit']
    directory = exports / (Path(manifest['binaries']['game']['filename']).stem + manifest['export_suffix'])
    assembly = (directory / (audit['function_rva'] + '.asm')).read_text()
    lower, upper = int(audit['start_rva'], 16), int(audit['end_rva_exclusive'], 16)
    instructions = {}
    for line in assembly.splitlines():
        if not line.strip():
            continue
        address, text = line.split('\t', 1)
        if lower <= int(address, 16) < upper:
            instructions[address] = text
    expected = {address for address, text in instructions.items()
                if '[' in text or text.startswith('CALL ')}
    recorded = [r['instruction_rva'] for r in audit['records']]
    require(len(recorded) == len(set(recorded)), 'Duplicate selection-access record')
    require(set(recorded) == expected,
            f'Selection access mismatch; missing={sorted(expected-set(recorded))}; extra={sorted(set(recorded)-expected)}')
    linked = set(audit['incoming_register_rows'])
    for record in audit['records']:
        ids = record['row_ids']
        require(set(ids) <= set(row_ids), f'Unknown row at {record["instruction_rva"]}')
        text = instructions[record['instruction_rva']]
        if record['kind'] == 'stack_save':
            require(not ids and '[RSP +' in text and text.startswith(('MOV ', 'MOVAPS ')),
                    'Unjustified stack exclusion')
        else:
            require(bool(ids), f'Unclassified access: {record["instruction_rva"]}')
        linked.update(ids)
    selection_rows = {r['id'] for r in inputs + outputs if r['scope'] == 'selection'}
    require(selection_rows <= linked, f'Unlinked selection rows: {selection_rows-linked}')
    require({c['id'] for c in manifest['float_constants']} <= linked, 'Unlinked selection constant')
    result['catalog'] = {
        'inputs': len(inputs), 'outputs': len(outputs),
        'selection_constants': len(manifest['float_constants']),
        'explicit_input_columns': 5, 'explicit_output_columns': 5,
        'source_map_hashes_checked': len(inherited),
        'selection_instructions': len(instructions),
        'selection_memory_or_call_instructions': len(expected),
        'excluded_stack_saves': sum(r['kind'] == 'stack_save' for r in audit['records']),
        'coverage_rows_open': [r['id'] for r in manifest['coverage'] if r['status'] == 'open'],
        'complete_catalog': False,
        'scope': audit['claim_limit'],
    }
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--map', type=Path, required=True)
    parser.add_argument('--binaries-dir', type=Path, required=True)
    parser.add_argument('--exports-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        result = verify_catalog(args.map, args.binaries_dir, args.exports_dir)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f'Catalog verification failed: {error}\n')
    rendered = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end='')


if __name__ == '__main__':
    main()
