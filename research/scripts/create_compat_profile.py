#!/usr/bin/env python3
"""Snapshot reviewed maps and their exact baseline binaries into a static audit profile.

Requires fingerprint-matched local Ghidra exports. Does not approve runtime use.
The output is created exclusively: an existing profile is never overwritten.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from compatibility import (decode, fingerprint, number, operand_value,
                           pattern_for, raw, scan, sha, validate_profile,
                           Cs, CS_ARCH_X86, CS_MODE_64, CS_GRP_CALL, X86_OP_IMM, X86_OP_MEM, X86_REG_RIP)
from scan_pe import PEImage


def read_json(path):
    return json.loads(path.read_text())


def legacy_ranges(image, assembly_path, function):
    """Recover schema-1 export extents from individually decoded instruction starts."""
    starts = [number(line.split("\t", 1)[0]) for line in assembly_path.read_text().splitlines() if line.strip()]
    if not starts or starts != sorted(set(starts)) or starts[0] != number(function["rva"]):
        raise ValueError("Invalid schema-1 instruction listing")
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    ranges = []
    for address in starts:
        if not image.executable(address):
            raise ValueError("Instruction listing points outside executable memory")
        section = image.section(address)
        instruction = next(decoder.disasm(raw(image, address, min(15, section.rva + section.raw_size - address)), address), None)
        if instruction is None:
            raise ValueError("Schema-1 instruction does not decode")
        end = address + instruction.size - 1
        if ranges and address <= number(ranges[-1]["end_rva_inclusive"]):
            raise ValueError("Overlapping schema-1 instructions")
        if ranges and address == number(ranges[-1]["end_rva_inclusive"]) + 1:
            ranges[-1]["end_rva_inclusive"] = hex(end)
        else:
            ranges.append({"start_rva": hex(address), "end_rva_inclusive": hex(end)})
    if sum(number(r["end_rva_inclusive"]) - number(r["start_rva"]) + 1 for r in ranges) != function["body_address_count"]:
        raise ValueError("Decoded schema-1 extent disagrees with Ghidra body size")
    return ranges


def make_function(image, entry, ranges, vtables):
    spans = [(entry + r["offset"], entry + r["offset"] + r["size"]) for r in ranges]
    instructions = [i for a, b in spans for i in decode(image, a, b - a)]
    if not instructions or instructions[0].address != entry:
        raise ValueError("Export does not start at the method entry")
    prefix = []
    for instruction in instructions:
        if prefix and instruction.address != prefix[-1].address + prefix[-1].size:
            break
        prefix.append(instruction)
        if sum(i.size for i in prefix) >= 64:
            break
    data_checks = []
    for instruction in instructions:
        for index, operand in enumerate(instruction.operands):
            if operand.type != X86_OP_MEM or operand.mem.base != X86_REG_RIP:
                continue
            target = instruction.address + instruction.size + operand.mem.disp
            try:
                section = image.section(target)
                if section.characteristics & (0x20000000 | 0x80000000):
                    continue
                value = None
                if instruction.mnemonic == "lea":
                    # Check complete short printable string anchors, not arbitrary LEA pointees.
                    literal = image.cstring(target, 128)
                    if 4 <= len(literal) < 128 and literal.isascii() and literal.isprintable():
                        value = literal.encode("ascii") + b"\0"
                elif 1 <= operand.size <= 16:
                    value = raw(image, target, operand.size)
                if value is not None:
                    data_checks.append({"offset": instruction.address - entry, "instruction_size": instruction.size,
                                        "operand": index, "hex": value.hex()})
            except ValueError:
                continue
    coverage = image.coverage(entry)
    baseline = {"rva": hex(entry), "normalized_sha256": fingerprint(image, entry, ranges)}
    if coverage["status"] == "unwind_start":
        baseline["unwind_size"] = number(coverage["end_rva"]) - entry
    return {"kind": "function", "baseline": baseline, "ranges": ranges,
            "signature": pattern_for(prefix, spans), "vtables": vtables, "data_checks": data_checks}


def context_pattern(image, entry, ranges, captures):
    """Create an operand-independent context, unique within the baseline parent."""
    spans = [(entry + r["offset"], entry + r["offset"] + r["size"]) for r in ranges]
    for start, end in spans:
        if not all(start <= number(c["at"]) < end for c in captures):
            continue
        instructions = decode(image, start, end - start)
        indexes = {i.address: n for n, i in enumerate(instructions)}
        capture_indexes = [indexes[number(c["at"])] for c in captures]
        low, high = min(capture_indexes), max(capture_indexes)
        for padding in range(2, 13):
            first, last = max(0, low - padding), min(len(instructions), high + padding + 1)
            sequence = instructions[first:last]
            adjusted = [dict({k: v for k, v in c.items() if k != "at"},
                             instruction=indexes[number(c["at"])] - first) for c in captures]
            pattern = pattern_for(sequence, captures=adjusted)
            matches = scan(image, pattern, spans)
            if matches == [sequence[0].address]:
                return sequence, adjusted, pattern
    raise ValueError("Cannot construct a unique baseline operand context")


def create_profile(baseline_dir, binaries, exports, contracts):
    spec = read_json(contracts) if contracts else {}
    surveys = {key: read_json(baseline_dir / f"{stem}.pe.json")
               for key, stem in [("game", "call_to_arms"), ("editor", "call_to_arms_ed")]}
    build_ids = {s["binary"]["steam_build_id"] for s in surveys.values()}
    if len(build_ids) != 1:
        raise ValueError("Game/editor surveys identify different builds")
    profile = {"schema_version": 1, "baseline_build_id": build_ids.pop(), "runtime_verified": False,
               "binaries": {}, "sources": {}, "mappings": [], "capabilities": [],
               "limitations": ["Instruction fingerprints do not prove semantic equivalence or callable ABI",
                               "Only explicit operand contracts validate individual fields",
                               "Uncovered source records remain visible as NOT_CHECKED",
                               "External calls/data not covered by explicit references still need review"]}
    images, exports_by_hash, tables = {}, {}, {}
    for key, survey in surveys.items():
        info = survey["binary"]
        data = (binaries / info["name"]).read_bytes()
        if sha(data) != info["sha256"]:
            raise ValueError(f"Baseline binary fingerprint mismatch: {info['name']}")
        profile["binaries"][key] = {"filename": info["name"], "sha256": info["sha256"]}
        images[key] = PEImage(data)
        exports_by_hash[info["sha256"]] = {}
        tables[key] = []
        for mangled_name in sorted({base["mangled_name"] for descriptors in survey["types"].values()
                                    for d in descriptors for loc in d["locators"] for base in loc["bases"]}):
            for descriptor in images[key].rtti(mangled_name):
                for loc in descriptor["locators"]:
                    for table in loc["vtables"]:
                        tables[key].append(dict(table, mangled_name=mangled_name,
                                                subobject_offset=loc["subobject_offset"], bases=loc["bases"],
                                                constructor_displacement=loc["constructor_displacement"]))
    for path in sorted(exports.glob("*/export.json")):
        report = read_json(path)
        if report.get("schema_version") not in (1, 2):
            continue
        index = exports_by_hash.get(report["binary"]["sha256"])
        if index is None:
            continue
        for function in report["functions"]:
            if function["decompile_completed"]:
                if report["schema_version"] == 1:
                    key = next(k for k, v in profile["binaries"].items() if v["sha256"] == report["binary"]["sha256"])
                    try:
                        function["body_ranges"] = legacy_ranges(images[key], path.parent / (function["rva"] + ".asm"), function)
                    except (ValueError, OSError):
                        continue
                index[number(function["rva"])] = function
    maps = {}
    for path in sorted(baseline_dir.glob("*-map.json")):
        data = read_json(path)
        if data.get("steam_build_id") != profile["baseline_build_id"]:
            raise ValueError(f"Source map build mismatch: {path.name}")
        for key, info in data.get("binaries", {}).items():
            if info["sha256"] != profile["binaries"][key]["sha256"]:
                raise ValueError(f"Source map fingerprint mismatch: {path.name}")
        maps[path.stem] = data
        profile["sources"][path.name] = sha(path.read_bytes())
    if not maps:
        raise ValueError("No reviewed *-map.json files found")
    function_cache = {}

    def function_mapping(key, entry):
        cache_key = key, entry
        if cache_key not in function_cache:
            exported = exports_by_hash[profile["binaries"][key]["sha256"]].get(entry)
            if not exported:
                raise ValueError("No fingerprint-matched Ghidra function export")
            ranges = [{"offset": number(r["start_rva"]) - entry,
                       "size": number(r["end_rva_inclusive"]) - number(r["start_rva"]) + 1}
                      for r in exported["body_ranges"]]
            if any(r["offset"] < 0 for r in ranges):
                raise ValueError("Function has code before its entry; needs a dedicated locator")
            vtables = [{"mangled_name": t["mangled_name"], "subobject_offset": t["subobject_offset"], "slot": slot["slot"]}
                       for t in tables[key] for slot in t["methods"] if number(slot["rva"]) == entry]
            function_cache[cache_key] = make_function(images[key], entry, ranges, vtables)
        # Each source record gets its own stable ID and dependency identity.
        return json.loads(json.dumps(function_cache[cache_key]))

    for stem, data in maps.items():
        for index, item in enumerate(data.get("methods", [])):
            identifier = item.get("id", item.get("proposed_role", str(index)))
            entries = item.get("entry_rvas", {k: item[k + "_rva"] for k in images if k + "_rva" in item})
            for key, value in entries.items():
                base = {"id": f"{key}/{stem}/methods/{identifier}", "binary": key,
                        "required": True, "source": f"{stem}.json#/methods/{index}"}
                try:
                    base.update(function_mapping(key, number(value)))
                except ValueError as error:
                    base.update(kind="uncovered", baseline={"rva": value}, reason=str(error))
                profile["mappings"].append(base)
        for index, item in enumerate(data.get("enums", [])):
            for key, rva in item["table_rvas"].items():
                profile["mappings"].append({"id": f"{key}/{stem}/enums/{item['id']}", "kind": "enum", "binary": key,
                                            "source": f"{stem}.json#/enums/{index}", "required": True,
                                            "baseline": {"rva": rva, "values": item["values"]}})
        for index, item in enumerate(data.get("byte_sequences", [])):
            entry, key = number(item["rva"]), item["binary"]
            if raw(images[key], entry, len(bytes.fromhex(item["hex"]))) != bytes.fromhex(item["hex"]):
                raise ValueError(f"Source byte-sequence mismatch: {stem}:{index}")
            if f"{stem}/{item.get('id', index)}" not in spec.get("code_sequences", []):
                profile["mappings"].append({"id": f"coverage/{stem}/byte_sequences/{item.get('id', index)}",
                                            "kind": "uncovered", "required": False,
                                            "source": f"{stem}.json#/byte_sequences/{index}", "baseline": item,
                                            "reason": "Raw data or unclassified bytes need an independent cross-build locator"})
                continue
            mapping = make_function(images[key], entry, [{"offset": 0, "size": len(bytes.fromhex(item["hex"]))}], [])
            mapping.update(id=f"{key}/{stem}/byte_sequences/{item.get('id', index)}", kind="sequence", binary=key,
                           required=True, source=f"{stem}.json#/byte_sequences/{index}")
            profile["mappings"].append(mapping)
        for index, item in enumerate(data.get("resource_members", [])):
            profile["mappings"].append({"id": f"resource/{stem}/{item['member']}", "kind": "resource",
                                        "archive": item["archive"], "member": item["member"], "required": True,
                                        "source": f"{stem}.json#/resource_members/{index}",
                                        "baseline": {"sha256": item["sha256"], "size": item["size"]}})
        for category in ["fields", "inline_regions", "float_constants", "pointer_string_tables", "rtti_vtables",
                         "classification_tables", "kind_descriptors"]:
            items = data.get(category, [])
            items = items.items() if isinstance(items, dict) else enumerate(items)
            for index, item in items:
                if not isinstance(item, dict):
                    item = {"value": item}
                profile["mappings"].append({"id": f"coverage/{stem}/{category}/{item.get('id', index)}",
                                            "kind": "uncovered", "required": False,
                                            "source": f"{stem}.json#/{category}/{index}", "baseline": item,
                                            "reason": "No dedicated cross-build contract for this source record; "
                                                      "method fingerprints do not validate its interpretation"})
    for key, values in tables.items():
        for table in values:
            profile["mappings"].append({"id": f"{key}/rtti/{table['mangled_name']}/{table['subobject_offset']}/{table['rva']}",
                                        "kind": "rtti", "binary": key, "required": True,
                                        "mangled_name": table["mangled_name"], "subobject_offset": table["subobject_offset"],
                                        "baseline": {"rva": table["rva"], "bases": table["bases"],
                                                     "constructor_displacement": table["constructor_displacement"],
                                                     "scanned_slots": len(table["methods"])}})
    # A virtual caller supplies an independent location for small leaf wrappers,
    # including methods for which .pdata contains no entry.
    anchors = [m for m in profile["mappings"] if m["kind"] == "function" and m["vtables"]]
    call_anchors, visited = {}, set()
    needed = {(m["binary"], number(m["baseline"]["rva"])) for m in profile["mappings"]
              if m["kind"] == "function" and not m["vtables"]}
    for anchor in anchors:
        key, parent = anchor["binary"], number(anchor["baseline"]["rva"])
        if (key, parent) in visited:
            continue
        visited.add((key, parent))
        spans = [(parent + r["offset"], parent + r["offset"] + r["size"]) for r in anchor["ranges"]]
        for start, end in spans:
            instructions = decode(images[key], start, end - start)
            for index, instruction in enumerate(instructions):
                if not instruction.group(CS_GRP_CALL) or instruction.operands[0].type != X86_OP_IMM:
                    continue
                address_key = key, instruction.operands[0].imm
                if address_key not in needed or address_key in call_anchors:
                    continue
                first, last = max(0, index - 2), min(len(instructions), index + 3)
                sequence = instructions[first:last]
                pattern = pattern_for(sequence)
                if scan(images[key], pattern, spans) == [sequence[0].address]:
                    call_anchors[address_key] = {"parent": anchor["id"], "pattern": pattern, "instruction": index - first}
    for m in profile["mappings"]:
        if m["kind"] == "function" and not m["vtables"]:
            selected = call_anchors.get((m["binary"], number(m["baseline"]["rva"])))
            if selected:
                m["callers"] = [selected]
    if contracts:
        if spec["baseline_build_id"] != profile["baseline_build_id"]:
            raise ValueError("Operand contract build mismatch")
        profile["sources"][contracts.name] = sha(contracts.read_bytes())
        by_id = {m["id"]: m for m in profile["mappings"]}
        for item in spec["operands"]:
            for key, location in item["at"].items():
                parent_id = f"{key}/{item['parent']}"
                parent = by_id[parent_id]
                if parent["kind"] != "function":
                    raise ValueError(f"Operand parent unavailable: {parent_id}")
                captures = [dict(c, at=location) for c in item["captures"]]
                sequence, adjusted, pattern = context_pattern(images[key], number(parent["baseline"]["rva"]),
                                                              parent["ranges"], captures)
                actual = {c["name"]: operand_value(sequence[c["instruction"]], c) for c in adjusted}
                if actual != {name: number(value) for name, value in item["expected"].items()}:
                    raise ValueError(f"Baseline operand disagrees with reviewed contract: {item['id']}: {actual}")
                profile["mappings"].append({"id": f"{key}/contracts/{item['id']}", "kind": "operand", "binary": key,
                                            "required": True, "source": item["source"], "parent": parent_id,
                                            "parent_size": parent["ranges"][0]["size"], "pattern": pattern,
                                            "captures": adjusted, "baseline": {"rva": hex(sequence[0].address), "values": actual}})
        profile["capabilities"] = list(spec["capabilities"])
        available_ids = {m["id"] for m in profile["mappings"]}
        for capability in spec["capabilities"]:
            if capability["id"].startswith("game/"):
                requires = [key.replace("game/", "editor/", 1) for key in capability["requires"]]
                if set(requires) <= available_ids:
                    profile["capabilities"].append({"id": capability["id"].replace("game/", "editor/", 1), "requires": requires})
    validate_profile(profile)
    return profile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-dir", type=Path, required=True)
    parser.add_argument("--binaries-dir", type=Path, required=True)
    parser.add_argument("--exports-dir", type=Path, required=True)
    parser.add_argument("--contracts", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output exists; use a new profile path to preserve the reviewed baseline")
    try:
        profile = create_profile(args.baseline_dir, args.binaries_dir, args.exports_dir, args.contracts)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x") as output:
            output.write(json.dumps(profile, indent=2) + "\n")
    except (ValueError, KeyError, OSError) as error:
        parser.exit(2, f"Profile could not be created: {error}\n")
    print(f"Created {len(profile['mappings'])} contracts/coverage records: {args.output}")


if __name__ == "__main__":
    main()
