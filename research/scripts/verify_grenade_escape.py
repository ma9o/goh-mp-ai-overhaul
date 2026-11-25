#!/usr/bin/env python3
"""Replay the two-image grenade escape evidence using file-backed PE bytes."""
import argparse
import hashlib
import json
import zipfile
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86_const import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

from scan_pe import PEImage


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check(condition, label):
    if not condition:
        raise ValueError(label)


def read_at(image, rva, size):
    pos = image.offset(rva, size)
    return image.data[pos:pos + size]


def check_build(build, path, resource_dir):
    image = PEImage(path.read_bytes())
    fingerprint = digest(image.data)
    check(fingerprint == build["sha256"], f"PE fingerprint mismatch for {path}")
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    starts = set()
    for span in build["code"]:
        start = int(span["rva"], 16)
        raw = read_at(image, start, span["size"])
        insns = list(decoder.disasm(raw, start))
        check(image.executable(start) and digest(raw) == span["sha256"], span["id"])
        check(len(insns) == span["instructions"] and sum(i.size for i in insns) == len(raw),
              f"Incomplete decoding at {start:#x}")
        starts.update(i.address for i in insns)
    for edge in build["references"]:
        site, expected = (int(edge[k], 16) for k in ("site", "target"))
        check(site in starts, f"Reference outside decoded evidence at {site:#x}")
        insn = next(decoder.disasm(read_at(image, site, 15), site, 1))
        if edge["kind"] == "call":
            check(insn.mnemonic == "call" and insn.operands[0].type == X86_OP_IMM
                  and insn.operands[0].imm == expected, edge)
        else:
            check(edge["kind"] == "rip" and any(
                op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP
                and site + insn.size + op.mem.disp == expected for op in insn.operands), edge)
    for item in build["data"]:
        rva = int(item["rva"], 16)
        if item["encoding"] == "cstring":
            value = image.cstring(rva)
        elif item["encoding"] == "f32":
            value = image.read("<f", rva)[0]
        elif item["encoding"] == "u32":
            value = image.read("<I", rva)[0]
        elif item["encoding"] == "zerofill_f32":
            check(any(section.rva + section.raw_size <= rva
                      and rva + 4 <= section.rva + section.virtual_size
                      and section.characteristics & 0x80000000 for section in image.sections),
                  f"Expected writable PE zero-fill at {rva:#x}")
            value = 0.0
        else:
            check(item["encoding"] == "i32[32]", item["id"])
            value = list(image.read("<32i", rva))
        check(value == item["value"], item["id"])
    for binding in build["vtables"]:
        tables = [v for td in image.rtti(binding["type"]) for loc in td["locators"]
                  if loc["subobject_offset"] == 0 for v in loc["vtables"]
                  if v["rva"] == binding["rva"]]
        check(len(tables) == 1, binding["type"])
        slots = {m["slot"]: m["rva"] for m in tables[0]["methods"]}
        for slot, target in binding["slots"].items():
            check(slots.get(int(slot)) == target, f"{binding['type']} slot {slot}")
    for member in build.get("resource_members", []):
        with zipfile.ZipFile(resource_dir / member["archive"]) as archive:
            check(digest(archive.read(member["member"])) == member["sha256"], member["member"])
    return {"sha256": fingerprint, "code_spans": len(build["code"]),
            "distinct_instruction_starts": len(starts), "references": len(build["references"]),
            "typed_data_checks": len(build["data"]), "rtti_tables": len(build["vtables"]),
            "vtable_slots": sum(len(v["slots"]) for v in build["vtables"]),
            "resource_member_hashes": len(build.get("resource_members", []))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--updated", type=Path, required=True)
    parser.add_argument("--updated-resources", type=Path, required=True)
    args = parser.parse_args()
    raw = args.map.read_bytes()
    manifest = json.loads(raw)
    paths = {"25492057": args.baseline, "26734796": args.updated}
    check(set(manifest["builds"]) == set(paths), "Unexpected build set")
    checked = {build: check_build(manifest["builds"][build], path, args.updated_resources)
               for build, path in paths.items()}
    print(json.dumps({"status": "passed", "scope": "Static PE and read-only member checks",
                      "manifest_sha256": digest(raw), "builds": checked,
                      "runtime_verified": False, "runtime_allowed": False}, indent=2))


if __name__ == "__main__":
    main()
