#!/usr/bin/env python3
"""Read-only, dependency-free structural survey of GoH's x64 PE executables.

Results identify metadata and candidate entry points, not verified hook targets.
Only file-backed RVAs are read. No executable is loaded or modified.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import re
import struct
from dataclasses import asdict, dataclass
from pathlib import Path


TYPE_NAMES = {
    "eActor": ".?AVeActor@@",
    "Actor::eBrain": ".?AVeBrain@Actor@@",
    "Actor::eHumanBrain": ".?AVeHumanBrain@Actor@@",
    "Order::eBrainUpdate": ".?AUeBrainUpdate@Order@@",
    "Order::eMoveToCover": ".?AUeMoveToCover@Order@@",
    "Helper::eCmd_actor_to_cover": ".?AUeCmd_actor_to_cover@Helper@@",
    "Helper::eCmd_actor_state": ".?AUeCmd_actor_state@Helper@@",
}
ANCHORS = [
    "actor_to_cover", "actor_state", "advance_ratio", "retreat_ratio",
    "move_mode", "fire_mode", "BotApi", "CaptureFlag", "SeekAndDestroy",
    "QueryScene", "GetVar", "SetVar",
    "scene:quant:actor:bup", "scene:quant:actor:buq",
]


@dataclass(frozen=True)
class Section:
    name: str
    rva: int
    virtual_size: int
    raw_offset: int
    raw_size: int
    characteristics: int


class PEImage:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < 64 or data[:2] != b"MZ":
            raise ValueError("Not an MZ executable")
        pe = self.unpack("<I", 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("Missing PE signature")
        machine, count = self.unpack("<HH", pe + 4)
        optional_size = self.unpack("<H", pe + 20)[0]
        optional = pe + 24
        if machine != 0x8664 or self.unpack("<H", optional)[0] != 0x20B:
            raise ValueError("This scanner supports x64 PE32+ images only")
        if optional_size < 112 or optional + optional_size > len(data):
            raise ValueError("Truncated optional header")
        self.image_base = self.unpack("<Q", optional + 24)[0]
        self.image_size = self.unpack("<I", optional + 56)[0]
        self.timestamp = self.unpack("<I", pe + 8)[0]
        directory_count = self.unpack("<I", optional + 108)[0]
        directory_count = min(directory_count, (optional_size - 112) // 8)
        self.directories = [self.unpack("<II", optional + 112 + i * 8)
                            for i in range(directory_count)]
        self.sections = []
        for i in range(count):
            p = optional + optional_size + i * 40
            if p + 40 > len(data):
                raise ValueError("Truncated section table")
            name = data[p:p + 8].rstrip(b"\0").decode("ascii", errors="replace")
            virtual_size, rva, raw_size, raw = self.unpack("<IIII", p + 8)
            characteristics = self.unpack("<I", p + 36)[0]
            if raw_size and raw + raw_size > len(data):
                raise ValueError(f"Truncated section {name}")
            self.sections.append(Section(name, rva, virtual_size, raw, raw_size,
                                         characteristics))
        self.unwind_ranges = []
        if len(self.directories) > 3:
            rva, size = self.directories[3]
            if rva and size:
                if size % 12:
                    raise ValueError("Malformed x64 exception directory")
                pos = self.offset(rva, size)
                for p in range(pos, pos + size, 12):
                    start, end, info = self.unpack("<III", p)
                    if start and end > start:
                        self.unwind_ranges.append((start, end, info))
        self.unwind_ranges.sort()
        self.starts = [x[0] for x in self.unwind_ranges]

    def unpack(self, fmt: str, offset: int):
        if offset < 0 or offset + struct.calcsize(fmt) > len(self.data):
            raise ValueError(f"Out-of-bounds read at {offset:#x}")
        return struct.unpack_from(fmt, self.data, offset)

    def section(self, rva: int, size: int = 1) -> Section:
        if size < 1:
            raise ValueError("Read size must be positive")
        for s in self.sections:
            if s.rva <= rva and rva + size <= s.rva + s.raw_size:
                return s
        raise ValueError(f"RVA {rva:#x}+{size:#x} is not file-backed")

    def offset(self, rva: int, size: int = 1) -> int:
        s = self.section(rva, size)
        return s.raw_offset + rva - s.rva

    def rva(self, offset: int) -> int:
        for s in self.sections:
            if s.raw_offset <= offset < s.raw_offset + s.raw_size:
                return s.rva + offset - s.raw_offset
        raise ValueError(f"File offset {offset:#x} is not in a section")

    def read(self, fmt: str, rva: int):
        return self.unpack(fmt, self.offset(rva, struct.calcsize(fmt)))

    def cstring(self, rva: int, limit: int = 2048) -> str:
        s = self.section(rva)
        start = self.offset(rva)
        end = min(start + limit, s.raw_offset + s.raw_size)
        nul = self.data.find(b"\0", start, end)
        if nul < 0:
            raise ValueError("Unterminated string")
        return self.data[start:nul].decode("ascii", errors="replace")

    def executable(self, rva: int) -> bool:
        try:
            return bool(self.section(rva).characteristics & 0x20000000)
        except ValueError:
            return False

    def coverage(self, rva: int) -> dict:
        i = bisect.bisect_right(self.starts, rva) - 1
        if i >= 0:
            start, end, _ = self.unwind_ranges[i]
            if start <= rva < end:
                return {"status": "unwind_start" if rva == start else "inside_unwind_range",
                        "start_rva": hex(start), "end_rva": hex(end),
                        "offset_into_range": rva - start}
        return {"status": "no_unwind_range"}

    def occurrences(self, pattern: bytes):
        return (m.start() for m in re.finditer(re.escape(pattern), self.data))

    def literal(self, value: str) -> list[str]:
        results = []
        for pos in self.occurrences(value.encode("ascii") + b"\0"):
            if pos and self.data[pos - 1] != 0:
                continue  # Exclude suffixes of longer strings.
            try:
                results.append(hex(self.rva(pos)))
            except ValueError:
                pass
        return results

    def hierarchy(self, rva: int) -> list[dict]:
        signature, attributes, count, array = self.read("<IIII", rva)
        if signature != 0 or not 1 <= count <= 256:
            raise ValueError("Unexpected RTTI class hierarchy")
        result = []
        for i in range(count):
            desc = self.read("<I", array + i * 4)[0]
            td, contained, member, vbptr, vbase, attrs = self.read("<IIiiiI", desc)
            result.append({"mangled_name": self.cstring(td + 16),
                           "contained_bases": contained,
                           "pmd": {"member_displacement": member,
                                   "vbtable_pointer_displacement": vbptr,
                                   "vbtable_entry_displacement": vbase},
                           "attributes": hex(attrs)})
        return result

    def rtti(self, mangled: str) -> list[dict]:
        types = []
        for pos in self.occurrences(mangled.encode("ascii") + b"\0"):
            try:
                td = self.rva(pos - 16)
            except ValueError:
                continue
            locators = []
            for ref in self.occurrences(struct.pack("<I", td)):
                start = ref - 12
                try:
                    sig, offset, cd, type_rva, hierarchy, self_rva = self.unpack("<6I", start)
                    col = self.rva(start)
                    if sig != 1 or type_rva != td or self_rva != col:
                        continue
                    bases = self.hierarchy(hierarchy)
                except ValueError:
                    continue
                tables = []
                for vref in self.occurrences(struct.pack("<Q", self.image_base + col)):
                    try:
                        table_rva = self.rva(vref + 8)
                        if table_rva % 8 or self.executable(table_rva):
                            continue
                    except ValueError:
                        continue
                    methods = []
                    for slot in range(256):
                        try:
                            target = self.read("<Q", table_rva + slot * 8)[0] - self.image_base
                        except ValueError:
                            break
                        if not self.executable(target):
                            break
                        methods.append({"slot": slot, "rva": hex(target),
                                        "unwind": self.coverage(target)})
                    if methods:
                        tables.append({"rva": hex(table_rva), "methods": methods,
                                       "scan_limit_reached": len(methods) == 256})
                locators.append({"rva": hex(col), "subobject_offset": offset,
                                 "constructor_displacement": cd, "bases": bases,
                                 "vtables": tables})
            types.append({"type_descriptor_rva": hex(td), "locators": locators})
        return types


def old_addresses(repo: Path) -> list[dict]:
    result = []
    for relative in ["sdk/coa_hooks.h", "sdk/coa_lua_bridge.cpp", "sdk/coa_plugin_api.cpp"]:
        path = repo / relative
        if not path.exists():
            continue
        lines = path.read_text().splitlines()
        for number, line in enumerate(lines, 1):
            match = re.search(r"constexpr\s+uintptr_t\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+)", line)
            if not match:
                match = re.search(r"#define\s+(LUA_\w+)\s+(0x[0-9a-fA-F]+)", line)
            if match:
                result.append({"source": relative, "line": number,
                               "upstream_name": match[1], "rva": match[2].lower()})
    return result


def survey(binary: Path, repo: Path, build_id: str) -> dict:
    data = binary.read_bytes()
    pe = PEImage(data)
    legacy = old_addresses(repo)
    for item in legacy:
        rva = int(item["rva"], 16)
        item["unwind"] = pe.coverage(rva)
        item["executable_section"] = pe.executable(rva)
        item["function_identity_verified"] = False
    types = {name: pe.rtti(mangled) for name, mangled in TYPE_NAMES.items()}
    return {
        "schema_version": 1,
        "evidence_level": "static_structure_only",
        "warning": "Neither RTTI nor a function boundary proves a hook signature or behavior. "
                   "Unwind records may describe function fragments; leaf functions can lack records.",
        "binary": {"name": binary.name, "steam_build_id": build_id,
                   "sha256": hashlib.sha256(data).hexdigest(), "file_size": len(data),
                   "image_base": hex(pe.image_base), "image_size": pe.image_size,
                   "coff_timestamp": pe.timestamp},
        "sections": [asdict(s) for s in pe.sections],
        "unwind_record_count": len(pe.unwind_ranges),
        "rtti_type_string_count": len(re.findall(rb"\.\?A[VU][\x20-\x7e]{3,}\x00", data)),
        "types": types,
        "string_anchors": {anchor: pe.literal(anchor) for anchor in ANCHORS},
        "lua_version_strings": sorted(set(m.group().decode("ascii", errors="replace")
                                          for m in re.finditer(rb"Lua 5\.[0-9][\x20-\x7e]{0,80}", data))),
        "upstream_addresses": legacy,
    }


def candidate_methods(report: dict) -> list[tuple[str, str]]:
    """Prioritize HumanBrain overrides and the two relevant command classes."""
    tables = {}
    for name, descriptors in report["types"].items():
        for desc in descriptors:
            for locator in desc["locators"]:
                for table in locator["vtables"]:
                    tables[(name, locator["subobject_offset"])] = table["methods"]
    candidates = {}
    for (name, subobject), methods in tables.items():
        if name not in ["Actor::eHumanBrain", "Order::eBrainUpdate",
                        "Helper::eCmd_actor_to_cover", "Helper::eCmd_actor_state"]:
            continue
        base = tables.get(("Actor::eBrain", subobject), []) if name == "Actor::eHumanBrain" else []
        for method in methods:
            slot = method["slot"]
            if name == "Actor::eHumanBrain" and slot < len(base) and method["rva"] == base[slot]["rva"]:
                continue
            label = re.sub(r"\W+", "_", f"candidate_{name}_sub{subobject}_slot{slot}")
            candidates.setdefault(method["rva"], label)
    return sorted(candidates.items(), key=lambda item: int(item[0], 16))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    result = survey(args.binary, repo, args.build_id)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stem = args.binary.stem
    output = args.output_dir / f"{stem}.pe.json"
    output.write_text(json.dumps(result, indent=2) + "\n")
    candidates = candidate_methods(result)
    (args.output_dir / f"{stem}.candidates.tsv").write_text(
        "# RVA\tstructural label; NOT a verified function name\n" +
        "".join(f"{rva}\t{label}\n" for rva, label in candidates))
    print(f"{args.binary.name}: {len(candidates)} candidate methods; {output}")


if __name__ == "__main__":
    main()
