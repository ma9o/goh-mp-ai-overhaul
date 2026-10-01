"""Static compatibility contracts for file-backed x64 PE images.

Matching never grants runtime approval. Address operands are normalized only for
code comparison; constants, member displacements and internal branches survive.
"""
from __future__ import annotations

import hashlib
import re
import struct
import zipfile
from collections import Counter
from pathlib import Path

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_GRP_CALL, CS_GRP_JUMP
    from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
except ImportError as error:
    raise ImportError("Install research/requirements-compat.txt in a virtual environment") from error

from scan_pe import PEImage
from verify_fine_map import enum_values

GOOD = {"UNCHANGED", "RELOCATED"}
STATUSES = (*sorted(GOOD), "CONTRACT_CHANGED", "UNRESOLVED", "AMBIGUOUS", "NOT_CHECKED")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def number(value) -> int:
    return int(value, 0) if isinstance(value, str) else int(value)


def raw(image: PEImage, rva: int, size: int) -> bytes:
    start = image.offset(rva, size)
    return bytes(image.data[start:start + size])


def decode(image: PEImage, rva: int, size: int):
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    data = raw(image, rva, size)
    instructions = list(decoder.disasm(data, rva))
    if sum(i.size for i in instructions) != size:
        raise ValueError(f"Incomplete instruction decoding at {rva:#x}+{size:#x}")
    return instructions


def address_mask(instruction, spans: list[tuple[int, int]]) -> bytearray:
    """Mask RIP-relative data and external relative branches, never member offsets."""
    mask = bytearray([255] * instruction.size)
    if any(o.type == X86_OP_MEM and o.mem.base == X86_REG_RIP for o in instruction.operands):
        start, size = instruction.disp_offset, instruction.disp_size
        mask[start:start + size] = b"\0" * size
    if instruction.group(CS_GRP_CALL) or instruction.group(CS_GRP_JUMP):
        for operand in instruction.operands:
            if operand.type == X86_OP_IMM and not any(a <= operand.imm < b for a, b in spans):
                start, size = instruction.imm_offset, instruction.imm_size
                mask[start:start + size] = b"\0" * size
    return mask


def pattern_for(instructions, spans=(), captures=()) -> str:
    tokens = []
    capture_by_index = {}
    for c in captures:
        capture_by_index.setdefault(c["instruction"], []).append(c)
    for index, instruction in enumerate(instructions):
        mask = address_mask(instruction, list(spans))
        for capture in capture_by_index.get(index, []):
            operand = instruction.operands[capture["operand"]]
            if capture["attribute"] in ("displacement", "width") and operand.type == X86_OP_MEM:
                start, size = instruction.disp_offset, instruction.disp_size
            elif capture["attribute"] == "immediate" and operand.type == X86_OP_IMM:
                start, size = instruction.imm_offset, instruction.imm_size
            else:
                raise ValueError("Capture does not describe the decoded operand")
            mask[start:start + size] = b"\0" * size
        tokens.extend(f"{b:02x}" if m else "??" for b, m in zip(instruction.bytes, mask))
    return " ".join(tokens)


def compile_pattern(pattern: str):
    tokens = pattern.split()
    if not tokens or not any(t != "??" for t in tokens):
        raise ValueError("Empty or entirely wildcard signature")
    parts = []
    for t in tokens:
        if t == "??":
            parts.append(b".")
        elif re.fullmatch(r"[0-9a-fA-F]{2}", t):
            parts.append(re.escape(bytes.fromhex(t)))
        else:
            raise ValueError(f"Invalid signature token: {t}")
    # Lookahead preserves overlapping matches. DOTALL lets wildcards match 0x0a.
    return re.compile(b"(?=" + b"".join(parts) + b")", re.DOTALL)


def scan(image: PEImage, pattern: str, spans=None) -> list[int]:
    regex = compile_pattern(pattern)
    if spans is None:
        spans = [(s.rva, s.rva + s.raw_size) for s in image.sections
                 if s.characteristics & 0x20000000 and s.raw_size]
    return sorted({start + match.start() for start, end in spans
                   for match in regex.finditer(raw(image, start, end - start))})


def fingerprint(image: PEImage, entry: int, ranges: list[dict]) -> str:
    spans = [(entry + r["offset"], entry + r["offset"] + r["size"]) for r in ranges]
    normalized = bytearray()
    for start, end in spans:
        normalized.extend(struct.pack("<II", start - entry, end - start))
        for instruction in decode(image, start, end - start):
            mask = address_mask(instruction, spans)
            normalized.extend(b & m for b, m in zip(instruction.bytes, mask))
    return sha(normalized)


def operand_value(instruction, capture: dict) -> int:
    operand = instruction.operands[capture["operand"]]
    attr = capture["attribute"]
    if attr == "displacement" and operand.type == X86_OP_MEM:
        value = operand.mem.disp
    elif attr == "width" and operand.type == X86_OP_MEM:
        value = operand.size
    elif attr == "immediate" and operand.type == X86_OP_IMM:
        value = operand.imm
    else:
        raise ValueError(f"Operand no longer has expected {attr} representation")
    value = value & number(capture["mask"]) if "mask" in capture else value
    return value + capture.get("add", 0)


class Audit:
    def __init__(self, profile: dict, binaries: Path, resources: Path | None = None):
        validate_profile(profile)
        self.profile, self.resources = profile, resources
        self.images, self.binary_info, self.results = {}, {}, {}
        self.rtti_cache, self.scan_cache = {}, {}
        self.mappings = {m["id"]: m for m in profile["mappings"]}
        for key, info in profile["binaries"].items():
            record = {"filename": info["filename"], "baseline_sha256": info["sha256"]}
            try:
                data = (binaries / info["filename"]).read_bytes()
                record.update(sha256=sha(data), matches_baseline=sha(data) == info["sha256"])
                self.images[key] = PEImage(data)
            except (OSError, ValueError, struct.error) as error:
                record["error"] = str(error)
            self.binary_info[key] = record

    def tables(self, binary: str, name: str) -> list[dict]:
        key = (binary, name)
        if key not in self.rtti_cache:
            self.rtti_cache[key] = [dict(t, subobject_offset=l["subobject_offset"],
                                         bases=l["bases"], constructor_displacement=l["constructor_displacement"])
                                   for d in self.images[binary].rtti(name)
                                   for l in d["locators"] for t in l["vtables"]]
        return self.rtti_cache[key]

    def signature(self, binary: str, pattern: str) -> list[int]:
        key = (binary, pattern)
        if key not in self.scan_cache:
            self.scan_cache[key] = scan(self.images[binary], pattern)
        return self.scan_cache[key]

    def evaluate(self, mapping_id: str) -> dict:
        if mapping_id in self.results:
            return self.results[mapping_id]
        m = self.mappings[mapping_id]
        result = {"id": mapping_id, "kind": m["kind"], "source": m.get("source"),
                  "required": m.get("required", True), "status": "NOT_CHECKED",
                  "baseline": m.get("baseline", {}), "current": {}, "reasons": []}
        self.results[mapping_id] = result
        key = m.get("binary")
        if key:
            result["binary"] = key
        if key and key not in self.images:
            result["reasons"] = [self.binary_info[key].get("error", "Binary unavailable")]
            return result
        try:
            getattr(self, "check_" + m["kind"])(m, result)
        except (ValueError, OSError, KeyError, IndexError, struct.error, zipfile.BadZipFile) as error:
            result["status"] = "NOT_CHECKED"
            result["reasons"].append(f"Check could not complete: {error}")
        return result

    @staticmethod
    def finish(result: dict, rva: int, changed=()):
        result["current"]["rva"] = hex(rva)
        if changed:
            result["status"] = "CONTRACT_CHANGED"
            result["reasons"].extend(changed)
        else:
            result["status"] = ("UNCHANGED" if rva == number(result["baseline"]["rva"])
                                else "RELOCATED")

    @staticmethod
    def unique(result: dict, candidates: list[int]) -> bool:
        result["candidates"] = [hex(c) for c in sorted(set(candidates))]
        if len(set(candidates)) != 1:
            result["status"] = "AMBIGUOUS" if candidates else "UNRESOLVED"
            result["reasons"].append(f"Expected one candidate; found {len(set(candidates))}")
            return False
        return True

    def check_function(self, m, result):
        key, baseline = m["binary"], m["baseline"]
        image = self.images[key]
        exact = self.binary_info[key]["matches_baseline"]
        structural, relationships = set(), []
        for locator in m.get("vtables", []):
            tables = [t for t in self.tables(key, locator["mangled_name"])
                      if t["subobject_offset"] == locator["subobject_offset"]]
            targets = {number(t["methods"][locator["slot"]]["rva"]) for t in tables
                       if locator["slot"] < len(t["methods"])}
            structural.update(targets)
            relationships.append((locator, targets))
        call_relationships = []
        for locator in m.get("callers", []):
            caller = self.evaluate(locator["parent"])
            targets = set()
            if "rva" in caller["current"] and caller["status"] in GOOD | {"CONTRACT_CHANGED"}:
                parent = number(caller["current"]["rva"])
                parent_mapping = self.mappings[locator["parent"]]
                spans = [(parent + r["offset"], parent + r["offset"] + r["size"]) for r in parent_mapping["ranges"]]
                starts = {i.address for start, end in spans for i in decode(image, start, end - start)}
                sites = [p for p in scan(image, locator["pattern"], spans) if p in starts]
                if len(sites) == 1:
                    instructions = decode(image, sites[0], len(locator["pattern"].split()))
                    call = instructions[locator["instruction"]]
                    if call.group(CS_GRP_CALL) and call.operands[0].type == X86_OP_IMM:
                        targets.add(call.operands[0].imm)
            structural.update(targets)
            call_relationships.append((locator, targets))
        if exact:
            matches = []
        elif structural:
            pattern = compile_pattern(m["signature"])
            matches = [p for p in structural if pattern.match(raw(image, p, len(m["signature"].split())))]
        else:
            matches = self.signature(key, m["signature"])
        if exact:
            candidates = [number(baseline["rva"])]
        elif structural:
            candidates = sorted(structural.intersection(matches) or structural)
        else:
            candidates = matches
        if not self.unique(result, candidates):
            return
        target = candidates[0]
        result["current"]["rva"] = hex(target)
        coverage = image.coverage(target)
        result["current"]["entry_evidence"] = coverage["status"]
        if not image.executable(target):
            result["status"] = "UNRESOLVED"
            result["reasons"].append("Candidate is not in executable file-backed memory")
            return
        if (not exact and m["kind"] != "sequence" and coverage["status"] != "unwind_start"
                and target not in structural):
            result["status"] = "NOT_CHECKED"
            result["reasons"].append("Signature candidate lacks an independent entry anchor; inspect in Ghidra")
            return
        changes = []
        for locator, targets in relationships:
            if targets != {target}:
                changes.append(f"Vtable relationship changed: {locator['mangled_name']} "
                               f"subobject {locator['subobject_offset']:#x} slot {locator['slot']}")
        for locator, targets in call_relationships:
            if targets != {target}:
                changes.append(f"Direct-call relationship not recovered: {locator['parent']}")
        if coverage["status"] == "unwind_start" and baseline.get("unwind_size") is not None:
            size = number(coverage["end_rva"]) - target
            result["current"]["unwind_size"] = size
            if size != baseline["unwind_size"]:
                changes.append(f"Unwind extent changed: {baseline['unwind_size']} -> {size} bytes")
        try:
            actual = fingerprint(image, target, m["ranges"])
            result["current"]["normalized_sha256"] = actual
            if actual != baseline["normalized_sha256"]:
                changes.append("Recorded instruction fingerprint changed (behavioral meaning needs review)")
        except ValueError as error:
            changes.append(f"Recorded code extent no longer decodes: {error}")
        for reference in m.get("data_checks", []):
            try:
                instruction = decode(image, target + reference["offset"], reference["instruction_size"])[0]
                operand = instruction.operands[reference["operand"]]
                if operand.type != X86_OP_MEM or operand.mem.base != X86_REG_RIP:
                    raise ValueError("Expected a RIP-relative operand")
                data_rva = instruction.address + instruction.size + operand.mem.disp
                expected = bytes.fromhex(reference["hex"])
                if raw(image, data_rva, len(expected)) != expected:
                    changes.append(f"Referenced constant/string changed at instruction +{reference['offset']:#x}")
            except (ValueError, IndexError) as error:
                changes.append(f"Data reference at +{reference['offset']:#x} could not be checked: {error}")
        self.finish(result, target, changes)
        result["scope"] = "Instruction shape/constants only; masked external address targets and ABI are not proven"

    check_sequence = check_function

    def check_operand(self, m, result):
        parent = self.evaluate(m["parent"])
        if "rva" not in parent["current"] or parent["status"] in {"AMBIGUOUS", "UNRESOLVED", "NOT_CHECKED"}:
            result["reasons"].append(f"Parent location unavailable: {m['parent']}")
            return
        target = number(parent["current"]["rva"])
        image = self.images[m["binary"]]
        parent_mapping = self.mappings[m["parent"]]
        # One unwind record can cover only a fragment. Use the fingerprinted
        # Ghidra body windows, never the whole section or only the first record.
        spans = [(target + r["offset"], target + r["offset"] + r["size"]) for r in parent_mapping["ranges"]]
        if (parent["current"].get("unwind_size") is not None and
                parent["current"]["unwind_size"] != parent_mapping["baseline"].get("unwind_size")):
            result["reasons"].append("Parent unwind extent changed; refresh its Ghidra body windows")
            return
        boundaries = {i.address for start, end in spans for i in decode(image, start, end - start)}
        candidates = [rva for rva in scan(image, m["pattern"], spans)
                      if rva in boundaries]
        if not self.unique(result, candidates):
            return
        location = candidates[0]
        sequence = decode(image, location, len(m["pattern"].split()))
        actual = {c["name"]: operand_value(sequence[c["instruction"]], c) for c in m["captures"]}
        result["current"]["values"] = actual
        changes = [f"{name}: {expected:#x} -> {actual[name]:#x}" for name, expected in
                   m["baseline"]["values"].items() if actual[name] != expected]
        self.finish(result, location, changes)
        result["scope"] = "Selected operands in baseline-relative Ghidra windows; owner/type interpretation remains analyst evidence"

    def check_enum(self, m, result):
        image, names = self.images[m["binary"]], list(m["baseline"]["values"])
        candidates = set()
        for string_rva in image.literal(names[0]):
            pointer = struct.pack("<Q", image.image_base + number(string_rva))
            for pos in image.occurrences(pointer):
                try:
                    rva = image.rva(pos)
                    if rva % 8 or image.executable(rva):
                        continue
                    values = enum_values(image, rva)
                    if list(values)[:len(names)] == names:
                        candidates.add(rva)
                except (ValueError, struct.error):
                    continue
        if not self.unique(result, sorted(candidates)):
            return
        rva = next(iter(candidates))
        actual = enum_values(image, rva)
        result["current"]["values"] = actual
        self.finish(result, rva, [] if actual == m["baseline"]["values"] else ["Enum names/values changed"])

    def check_rtti(self, m, result):
        tables = [t for t in self.tables(m["binary"], m["mangled_name"])
                  if t["subobject_offset"] == m["subobject_offset"]]
        if not self.unique(result, [number(t["rva"]) for t in tables]):
            return
        table = tables[0]
        changes = []
        for field, actual in [("bases", table["bases"]), ("constructor_displacement", table["constructor_displacement"]),
                              ("scanned_slots", len(table["methods"]))]:
            result["current"][field] = actual
            if actual != m["baseline"][field]:
                changes.append(f"RTTI/vtable {field} changed")
        self.finish(result, number(table["rva"]), changes)
        result["scope"] = "Executable-pointer extent is a heuristic, not a verified vtable size"

    def check_resource(self, m, result):
        if self.resources is None:
            result["reasons"].append("Supply --resources-dir to check this archive member")
            return
        with zipfile.ZipFile(self.resources / m["archive"]) as archive:
            data = archive.read(m["member"])
        result["current"] = {"sha256": sha(data), "size": len(data)}
        result["status"] = "UNCHANGED" if result["current"] == m["baseline"] else "CONTRACT_CHANGED"
        if result["status"] != "UNCHANGED":
            result["reasons"].append("Archive member content changed; effective mod override order is not checked")

    def check_uncovered(self, m, result):
        result["reasons"].append(m["reason"])

    def run(self, build_id: str) -> dict:
        for m in self.profile["mappings"]:
            self.evaluate(m["id"])
        rows = [self.results[m["id"]] for m in self.profile["mappings"]]
        capabilities = []
        for c in self.profile.get("capabilities", []):
            blocked = [key for key in c["requires"] if self.results[key]["status"] not in GOOD]
            capabilities.append({"id": c["id"], "static_checks_passed": not blocked,
                                 "blocked_by": blocked, "runtime_allowed": False,
                                 "runtime_reason": "No verified callable ABI/lifecycle or approved runtime profile"})
        return {"schema_version": 1, "baseline_build_id": self.profile["baseline_build_id"],
                "target_build_id": build_id, "binaries": self.binary_info,
                "scope": "Static mapping audit, not a runtime compatibility approval",
                "runtime_allowed": False, "summary": dict(Counter(r["status"] for r in rows)),
                "required_checks_passed": all(r["status"] in GOOD for r in rows if r["required"]),
                "mappings": rows, "capabilities": capabilities}


def validate_profile(profile: dict):
    if (not isinstance(profile, dict) or profile.get("schema_version") != 1
            or not str(profile.get("baseline_build_id", "")).isdecimal()):
        raise ValueError("Unsupported or invalid compatibility profile")
    if (not isinstance(profile.get("binaries"), dict) or not profile["binaries"]
            or not isinstance(profile.get("mappings"), list) or not profile["mappings"]):
        raise ValueError("Profile must contain binaries and mappings")
    for binary in profile["binaries"].values():
        if not isinstance(binary, dict):
            raise ValueError("Invalid binary record")
        if (Path(binary["filename"]).name != binary["filename"] or "\\" in binary["filename"]
                or not re.fullmatch(r"[0-9a-f]{64}", binary["sha256"])):
            raise ValueError("Invalid binary filename or fingerprint")
    if any(not isinstance(m, dict) or not isinstance(m.get("id"), str) for m in profile["mappings"]):
        raise ValueError("Mapping records require string IDs")
    mappings = {m["id"]: m for m in profile["mappings"]}
    if len(mappings) != len(profile["mappings"]):
        raise ValueError("Duplicate mapping IDs")
    for m in mappings.values():
        if m["kind"] not in {"function", "sequence", "operand", "enum", "rtti", "resource", "uncovered"}:
            raise ValueError(f"Unsupported mapping kind: {m['kind']}")
        if "binary" in m and m["binary"] not in profile["binaries"]:
            raise ValueError(f"Unknown binary for {m['id']}")
        if m["kind"] in {"function", "sequence"}:
            compile_pattern(m["signature"])
            if not m["ranges"] or any(r["offset"] < 0 or r["size"] <= 0 for r in m["ranges"]):
                raise ValueError(f"Invalid body windows: {m['id']}")
        if m["kind"] == "operand":
            compile_pattern(m["pattern"])
            parent = mappings.get(m["parent"])
            if not parent or parent["kind"] != "function" or parent["binary"] != m["binary"]:
                raise ValueError(f"Invalid operand parent: {m['id']}")
            names = [c["name"] for c in m["captures"]]
            if len(set(names)) != len(names) or set(names) != set(m["baseline"]["values"]):
                raise ValueError(f"Invalid captures: {m['id']}")
        for caller in m.get("callers", []):
            parent = mappings.get(caller["parent"])
            if (not parent or parent["kind"] != "function" or not parent.get("vtables")
                    or parent.get("callers") or parent["binary"] != m["binary"]):
                raise ValueError(f"Call anchor must be an independent virtual function: {m['id']}")
    for c in profile.get("capabilities", []):
        if not c["requires"] or not set(c["requires"]) <= mappings.keys():
            raise ValueError(f"Unknown/empty capability dependencies: {c['id']}")


def markdown(report: dict) -> str:
    lines = [f"# Mapping audit: {report['baseline_build_id']} → {report['target_build_id']}", "",
             report["scope"] + ". Runtime use remains blocked.", "",
             ", ".join(f"{key}: {value}" for key, value in sorted(report["summary"].items())), "",
             "| Mapping | Result | Baseline RVA | Current RVA | Detail |", "|---|---|---|---|---|"]
    for row in report["mappings"]:
        fields = [row["id"], row["status"], row["baseline"].get("rva", "—"),
                  row["current"].get("rva", "—"), "; ".join(row["reasons"])]
        lines.append("| " + " | ".join(str(x).replace("|", "\\|").replace("\n", " ") for x in fields) + " |")
    lines += ["", "## Capabilities", ""]
    for c in report["capabilities"]:
        lines.append(f"- **{c['id']}**: static checks {'passed' if c['static_checks_passed'] else 'blocked'}; "
                     f"runtime blocked. Missing checks: {', '.join(c['blocked_by']) or 'none'}.")
    return "\n".join(lines) + "\n"
