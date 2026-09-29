#!/usr/bin/env python3
"""Catalog one verified x64 property-registration stub shape without loading code.

The registrar RVA must be reviewed separately for each executable. This exact
shape is only one source of registrations; absent tokens are not evidence of an
absent property. It does not recover parser behavior or active configuration.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

from scan_pe import PEImage

# mov r9d, ID; lea r8, [name]; lea rdx, [registry]; lea rcx, [entry]; jmp registrar
STUB = re.compile(
    rb"\x41\xb9(.{4})\x4c\x8d\x05(.{4})\x48\x8d\x15(.{4})"
    rb"\x48\x8d\x0d(.{4})\xe9(.{4})", re.DOTALL)


def writable_storage(pe: PEImage, rva: int, size: int) -> bool:
    return 0 <= rva < rva + size <= pe.image_size and any(
        s.rva <= rva and rva + size <= s.rva + max(s.virtual_size, s.raw_size)
        and s.characteristics & 0x80000000 for s in pe.sections)


def catalog(binary: Path, build_id: str, registrar: int) -> dict:
    data = binary.read_bytes()
    pe = PEImage(data)
    if not pe.executable(registrar):
        raise ValueError("Registrar must lie in executable image storage")
    rows = []
    for match in STUB.finditer(data):
        try:
            rva = pe.rva(match.start())
            if not pe.section(rva, 32).characteristics & 0x20000000:
                continue
            targets = [rva + end + struct.unpack("<i", match[group])[0]
                       for end, group in [(13, 2), (20, 3), (27, 4), (32, 5)]]
            string, registry, entry, called = targets
            if called != registrar:
                continue
            name = pe.cstring(string)
            if not name or len(name) > 256 or not all(32 <= ord(c) < 127 for c in name):
                continue
            # Globals can be zero-filled and need not have file bytes.
            if not writable_storage(pe, registry, 8):
                continue
            if not writable_storage(pe, entry, 24):
                continue
            rows.append({"initializer_rva": hex(rva), "token": name,
                         "value": struct.unpack("<I", match[1])[0],
                         "registry_rva": hex(registry), "entry_rva": hex(entry),
                         "string_rva": hex(string), "stub_hex": match[0].hex()})
        except ValueError:
            continue
    return {
        "schema_version": 1, "steam_build_id": build_id,
        "binary": {"filename": binary.name, "sha256": hashlib.sha256(data).hexdigest(),
                   "image_base": hex(pe.image_base)},
        "registrar_rva": hex(registrar),
        "warning": "Exact instruction-pattern discovery only; not all registrations. "
                   "Token names/IDs are per registry, not globally interchangeable. "
                   "Stub bytes are retained for independent checking; function boundaries "
                   "and startup invocation are not inferred from this pattern alone.",
        "registrations": rows,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--registrar-rva", type=lambda s: int(s, 0), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = catalog(args.binary, args.build_id, args.registrar_rva)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Cataloged {len(report['registrations'])} property-registration stubs")


if __name__ == "__main__":
    main()
