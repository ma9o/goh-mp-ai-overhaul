#!/usr/bin/env python3
"""Check a reviewed map against local PE files and schema-2 Ghidra exports.

Checks fingerprints, enum tables, read-only float constants, exact entries and
instruction evidence locations. Does not prove analyst interpretations or ABI.
No executable is loaded or modified. Uses only the Python standard library.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from scan_pe import PEImage


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def readonly(image: PEImage, rva: int, size: int) -> None:
    require(not image.section(rva, size).characteristics & 0x80000000,
            f"Expected read-only data at {rva:#x}")


def enum_values(image: PEImage, table: int) -> dict[str, int]:
    values = {}
    for index in range(64):
        rva = table + index * 16
        readonly(image, rva, 12)
        pointer, value = image.read("<Qi", rva)
        if not pointer:
            return values
        name = image.cstring(pointer - image.image_base)
        readonly(image, pointer - image.image_base, len(name) + 1)
        require(name not in values, f"Duplicate enum name {name!r}")
        values[name] = value
    raise ValueError(f"Unterminated enum table at {table:#x}")


def verify(manifest_path: Path, binaries: Path, exports: Path) -> dict:
    manifest = json.loads(manifest_path.read_text())
    require(manifest["schema_version"] == 1, "Unsupported fine-map schema")
    images, functions, directories = {}, {}, {}
    result = {
        "schema_version": 1,
        "steam_build_id": manifest["steam_build_id"],
        "fine_map_sha256": digest(manifest_path),
        "runtime_verified": False,
        "scope": "Structural/data checks only; does not validate semantic interpretations, ABI, or completeness",
        "binaries": {},
    }
    for key, info in manifest["binaries"].items():
        binary = binaries / info["filename"]
        require(digest(binary) == info["sha256"], f"Binary hash mismatch: {binary.name}")
        images[key] = PEImage(binary.read_bytes())
        directories[key] = exports / (binary.stem + "-fine")
        report_path = directories[key] / "export.json"
        report = json.loads(report_path.read_text())
        require(report["schema_version"] == 2, f"Need schema-2 export: {key}")
        require(report["binary"]["sha256"] == info["sha256"], f"Export hash mismatch: {key}")
        require(int(report["binary"]["image_base"], 16) == images[key].image_base,
                f"Image base mismatch: {key}")
        require(all(c["exact_function_entry"] for c in report["candidates"]),
                f"Candidate is not an exact function entry: {key}")
        require(all(f["decompile_completed"] for f in report["functions"]),
                f"Incomplete decompilation: {key}")
        functions[key] = {f["rva"]: f for f in report["functions"]}
        result["binaries"][key] = {
            "filename": binary.name, "sha256": info["sha256"],
            "export_report_sha256": digest(report_path),
            "exact_candidates": len(report["candidates"]),
            "exported_functions": len(report["functions"]),
            "exported_basic_blocks": sum(len(f["basic_blocks"]) for f in report["functions"]),
            "checked_enums": 0, "checked_constants": 0,
            "checked_method_entries": 0, "checked_instruction_locations": 0,
        }

    def entry(key: str, rva: str) -> dict:
        require(rva in functions[key], f"Function not in export: {key} {rva}")
        require(images[key].executable(int(rva, 16)), f"Non-executable entry: {key} {rva}")
        return functions[key][rva]

    for item in manifest["enums"]:
        for key, table in item["table_rvas"].items():
            actual = enum_values(images[key], int(table, 16))
            require(actual == item["values"], f"Enum mismatch: {key} {item['id']}: {actual}")
            entry(key, item["parser_rvas"][key])
            result["binaries"][key]["checked_enums"] += 1
    for item in manifest["float_constants"]:
        key, rva = item["binary"], int(item["rva"], 16)
        readonly(images[key], rva, 4)
        actual = images[key].read("<f", rva)[0]
        require(actual == item["value"], f"Float mismatch: {key} {rva:#x}: {actual}")
        result["binaries"][key]["checked_constants"] += 1
    for method in manifest["methods"]:
        for key, rva in method["entry_rvas"].items():
            entry(key, rva)
            result["binaries"][key]["checked_method_entries"] += 1

    evidence_ids = set()
    for evidence in manifest["evidence"]:
        require(evidence["id"] not in evidence_ids, f"Duplicate evidence ID: {evidence['id']}")
        evidence_ids.add(evidence["id"])
        key, rva = evidence["binary"], evidence["function_rva"]
        function = entry(key, rva)
        assembly = (directories[key] / (rva + ".asm")).read_text()
        instructions = {int(line.split("\t", 1)[0], 16)
                        for line in assembly.splitlines() if line.strip()}
        for location in evidence["instruction_rvas"]:
            address = int(location, 16)
            require(address in instructions, f"Not an instruction start: {key} {location}")
            require(any(int(r["start_rva"], 16) <= address <= int(r["end_rva_inclusive"], 16)
                        for r in function["body_ranges"]), f"Outside function body: {key} {location}")
            result["binaries"][key]["checked_instruction_locations"] += 1
    for item in manifest["fields"] + manifest["inline_regions"]:
        require(set(item["evidence_ids"]) <= evidence_ids, f"Missing evidence: {item['id']}")
    result["status"] = "passed"
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--binaries-dir", type=Path, required=True)
    parser.add_argument("--exports-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = verify(args.map, args.binaries_dir, args.exports_dir)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f"Verification failed: {error}\n")
    rendered = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
