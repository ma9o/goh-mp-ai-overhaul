#!/usr/bin/env python3
"""Inventory native AI/entity RTTI roots in a local PE; never load executable code.

This is an intentionally broad structural worklist, not a semantic coverage claim.
Template adapters and newly encountered dependencies must be added through review.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

from scan_pe import PEImage

NATIVE = re.compile(rb"\.\?A[UV]e[A-Za-z0-9_@]+@@\x00")
SCOPE = re.compile(
    r"@(?:Actor|Human|Order|Inventory|Cover|Pather|Scene)@@|Accuracy|"
    r"e(?:Actor|GameEntity|Entity|Ordered|ComplexOrder|World|Scene|"
    r"SceneQuantTask|QuantTaskManager|QuantScheduledTask|QuantTask|EventScheduler|QuantTimer)@@")


def catalog(binary: Path, build_id: str) -> dict:
    data = binary.read_bytes()
    image = PEImage(data)
    names = sorted({m.group()[:-1].decode("ascii") for m in NATIVE.finditer(data)
                    if SCOPE.search(m.group()[:-1].decode("ascii"))})
    types = []
    for name in names:
        descriptors = image.rtti(name)
        tables = [dict(table, subobject_offset=locator["subobject_offset"],
                       locator_rva=locator["rva"], bases=locator["bases"])
                  for descriptor in descriptors for locator in descriptor["locators"]
                  for table in locator["vtables"]]
        types.append({"mangled_name": name,
                      "display_name": "::".join(reversed(name[4:-2].split("@"))),
                      "descriptor_rvas": [d["type_descriptor_rva"] for d in descriptors],
                      "vtables": tables})
    return {
        "schema_version": 1, "steam_build_id": build_id,
        "binary": {"filename": binary.name, "sha256": hashlib.sha256(data).hexdigest(),
                   "image_base": hex(image.image_base)},
        "scope_pattern": SCOPE.pattern,
        "warning": "Structural discovery only. Filtered roots are not all AI dependencies. "
                   "Vtable extents are executable-pointer scans, not verified slot counts. "
                   "No method semantics or runtime ABI is asserted.",
        "types": types,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = catalog(args.binary, args.build_id)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Cataloged {len(report['types'])} native type roots from {args.binary.name}")


if __name__ == "__main__":
    main()
