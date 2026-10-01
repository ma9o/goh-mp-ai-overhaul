"""Mutation tests using synthetic PE files only; never load executable code."""
import copy
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
try:
    from compatibility import (Audit, compile_pattern, decode, fingerprint, number,
                               pattern_for, scan, sha, validate_profile)
    from create_compat_profile import make_function, context_pattern, legacy_ranges
except ImportError as error:
    raise unittest.SkipTest(str(error))
from scan_pe import PEImage
from test_scan_pe import fixture


def put(data, rva, value):
    start = PEImage(data).offset(rva, len(value))
    data[start:start + len(value)] = value


def code(entry=0x1010, field=0x39c, mask=0xfdffffff, call=0x1080, string=0x2240):
    data = bytes.fromhex("48 8b 81 38 00 00 00 81 a1") + struct.pack("<II", field, mask)
    data += b"\xe8" + struct.pack("<i", call - (entry + len(data) + 5))
    data += b"\x48\x8d\x15" + struct.pack("<i", string - (entry + len(data) + 7))
    return (data + b"\xc3").ljust(0x30, b"\x90")


def binary():
    data = fixture()
    put(data, 0x1010, code())
    put(data, 0x1080, bytes.fromhex("b8 01 00 00 00 c3") + b"\x90" * 26)
    put(data, 0x2240, b"advance_ratio\0")
    put(data, 0x2260, b"first\0")
    put(data, 0x2270, b"second\0")
    put(data, 0x2300, struct.pack("<Qi4xQi4xQi4x", 0x140002260, 1, 0x140002270, 2, 0, 0))
    return data


def profile(data):
    image = PEImage(data)
    ranges = [{"offset": 0, "size": 0x30}]
    function = make_function(image, 0x1010, ranges, [{"mangled_name": ".?AVExample@@", "subobject_offset": 0, "slot": 0}])
    function.update(id="game/function", binary="game", required=True)
    captures = [{"at": "0x1017", "name": "offset", "operand": 0, "attribute": "displacement"},
                {"at": "0x1017", "name": "width", "operand": 0, "attribute": "width"},
                {"at": "0x1017", "name": "mask", "operand": 1, "attribute": "immediate", "mask": "0xffffffff"}]
    sequence, adjusted, pattern = context_pattern(image, 0x1010, ranges, captures)
    return {"schema_version": 1, "baseline_build_id": "100", "runtime_verified": False,
            "binaries": {"game": {"filename": "game.exe", "sha256": sha(data)}},
            "mappings": [function,
                         {"id": "game/field", "kind": "operand", "binary": "game", "parent": "game/function",
                          "required": True, "parent_size": 0x30, "captures": adjusted, "pattern": pattern,
                          "baseline": {"rva": hex(sequence[0].address), "values": {"offset": 0x39c, "width": 4, "mask": 0xfdffffff}}},
                         {"id": "game/enum", "kind": "enum", "binary": "game", "required": True,
                          "baseline": {"rva": "0x2300", "values": {"first": 1, "second": 2}}}],
            "capabilities": [{"id": "brain", "requires": ["game/function", "game/field"]},
                             {"id": "enum", "requires": ["game/enum"]}]}


class CompatibilityTests(unittest.TestCase):
    def setUp(self):
        self.data = binary()
        self.profile = profile(self.data)
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def run_audit(self, data=None, manifest=None, resources=None):
        (self.root / "game.exe").write_bytes(self.data if data is None else data)
        return Audit(self.profile if manifest is None else manifest, self.root, resources).run("101")

    @staticmethod
    def rows(report):
        return {r["id"]: r for r in report["mappings"]}

    def test_identical_build_passes_static_but_never_runtime(self):
        report = self.run_audit()
        self.assertEqual(report["summary"], {"UNCHANGED": 3})
        self.assertTrue(report["required_checks_passed"])
        self.assertFalse(report["runtime_allowed"])
        self.assertTrue(all(not c["runtime_allowed"] for c in report["capabilities"]))

    def test_relocated_function_normalizes_call_and_string_displacements(self):
        data = binary()
        put(data, 0x1010, b"\xcc" * 0x30)
        put(data, 0x10b0, code(entry=0x10b0))
        struct.pack_into("<II", data, 0xc00, 0x10b0, 0x10e0)
        put(data, 0x2108, struct.pack("<Q", 0x1400010b0))
        rows = self.rows(self.run_audit(data))
        self.assertEqual(rows["game/function"]["status"], "RELOCATED")
        self.assertEqual(rows["game/function"]["current"]["rva"], "0x10b0")
        self.assertEqual(rows["game/field"]["status"], "RELOCATED")

    def test_member_offset_change_names_the_failed_field_and_capability(self):
        data = binary()
        put(data, 0x1010, code(field=0x3ac))
        report = self.run_audit(data)
        rows = self.rows(report)
        self.assertEqual(rows["game/function"]["status"], "CONTRACT_CHANGED")
        self.assertEqual(rows["game/field"]["status"], "CONTRACT_CHANGED")
        self.assertEqual(rows["game/field"]["current"]["values"]["offset"], 0x3ac)
        self.assertFalse(report["capabilities"][0]["static_checks_passed"])
        self.assertTrue(report["capabilities"][1]["static_checks_passed"])

    def test_changed_mask_is_not_normalized_away(self):
        data = binary()
        put(data, 0x1010, code(mask=0xfbffffff))
        rows = self.rows(self.run_audit(data))
        self.assertEqual(rows["game/field"]["status"], "CONTRACT_CHANGED")
        self.assertEqual(rows["game/field"]["current"]["values"]["mask"], 0xfbffffff)

    def test_changed_referenced_string_is_reported(self):
        data = binary()
        put(data, 0x2240, b"retreat_ratio\0")
        rows = self.rows(self.run_audit(data))
        self.assertEqual(rows["game/function"]["status"], "CONTRACT_CHANGED")
        self.assertTrue(any("Referenced constant/string" in r for r in rows["game/function"]["reasons"]))

    def test_vtable_slot_reordering_is_not_silently_accepted(self):
        data = binary()
        put(data, 0x2108, struct.pack("<QQ", 0x140001080, 0x140001010))
        rows = self.rows(self.run_audit(data))
        self.assertEqual(rows["game/function"]["status"], "CONTRACT_CHANGED")

    def test_duplicate_signature_is_ambiguous_without_structural_anchor(self):
        data = binary()
        put(data, 0x10b0, code(entry=0x10b0))
        manifest = copy.deepcopy(self.profile)
        manifest["mappings"][0]["vtables"] = []
        rows = self.rows(self.run_audit(data, manifest))
        self.assertEqual(rows["game/function"]["status"], "AMBIGUOUS")
        self.assertEqual(len(rows["game/function"]["candidates"]), 2)
        self.assertEqual(rows["game/field"]["status"], "NOT_CHECKED")

    def test_duplicate_signature_can_be_disambiguated_by_independent_rtti(self):
        data = binary()
        put(data, 0x10b0, code(entry=0x10b0))
        self.assertEqual(self.rows(self.run_audit(data))["game/function"]["status"], "UNCHANGED")

    def test_missing_signature_without_rtti_does_not_reuse_old_rva(self):
        data = binary()
        put(data, 0x1010, b"\xcc" * 0x30)
        manifest = copy.deepcopy(self.profile)
        manifest["mappings"][0]["vtables"] = []
        rows = self.rows(self.run_audit(data, manifest))
        self.assertEqual(rows["game/function"]["status"], "UNRESOLVED")
        self.assertNotIn("rva", rows["game/function"]["current"])

    def test_signature_in_middle_of_unwind_range_is_not_a_verified_entry(self):
        data = binary()
        put(data, 0x1010, b"\x90" + code(entry=0x1011))
        manifest = copy.deepcopy(self.profile)
        manifest["mappings"][0]["vtables"] = []
        rows = self.rows(self.run_audit(data, manifest))
        self.assertEqual(rows["game/function"]["status"], "NOT_CHECKED")

    def test_enum_value_change_and_function_change_are_both_reported(self):
        data = binary()
        put(data, 0x2308, struct.pack("<i", 8))
        put(data, 0x1010, code(mask=0xffffffff))
        rows = self.rows(self.run_audit(data))
        self.assertEqual(rows["game/enum"]["status"], "CONTRACT_CHANGED")
        self.assertEqual(rows["game/enum"]["current"]["values"]["first"], 8)
        self.assertEqual(rows["game/field"]["status"], "CONTRACT_CHANGED")

    def test_enum_can_relocate_without_pointer_bytes_matching(self):
        data = binary()
        image = PEImage(data)
        old = bytes(data[image.offset(0x2300):image.offset(0x2300) + 48])
        put(data, 0x2300, b"\0" * 48)
        put(data, 0x2380, old)
        self.assertEqual(self.rows(self.run_audit(data))["game/enum"]["status"], "RELOCATED")

    def test_duplicate_enum_is_ambiguous_even_when_only_one_has_expected_values(self):
        data = binary()
        image = PEImage(data)
        put(data, 0x2380, bytes(data[image.offset(0x2300):image.offset(0x2300) + 48]))
        put(data, 0x2388, struct.pack("<i", 99))
        self.assertEqual(self.rows(self.run_audit(data))["game/enum"]["status"], "AMBIGUOUS")

    def test_missing_binary_does_not_stop_checks_for_other_binary(self):
        manifest = copy.deepcopy(self.profile)
        manifest["binaries"]["editor"] = {"filename": "missing.exe", "sha256": sha(self.data)}
        mapping = copy.deepcopy(manifest["mappings"][0])
        mapping.update(id="editor/function", binary="editor")
        manifest["mappings"].append(mapping)
        report = self.run_audit(manifest=manifest)
        self.assertEqual(self.rows(report)["editor/function"]["status"], "NOT_CHECKED")
        self.assertEqual(self.rows(report)["game/function"]["status"], "UNCHANGED")
        self.assertFalse(report["required_checks_passed"])

    def test_malformed_binary_becomes_per_mapping_failure(self):
        report = self.run_audit(b"not a PE")
        self.assertEqual(report["summary"], {"NOT_CHECKED": 3})
        self.assertFalse(report["required_checks_passed"])

    def test_resource_checked_even_when_executable_hash_is_identical(self):
        expected = b"original registry"
        manifest = copy.deepcopy(self.profile)
        manifest["mappings"].append({"id": "registry", "kind": "resource", "required": True,
                                     "archive": "gamelogic.pak", "member": "set/registry/test.reg",
                                     "baseline": {"sha256": sha(expected), "size": len(expected)}})
        with zipfile.ZipFile(self.root / "gamelogic.pak", "w") as archive:
            archive.writestr("set/registry/test.reg", b"new registry")
        report = self.run_audit(manifest=manifest, resources=self.root)
        self.assertEqual(self.rows(report)["registry"]["status"], "CONTRACT_CHANGED")
        report = self.run_audit(manifest=manifest)
        self.assertEqual(self.rows(report)["registry"]["status"], "NOT_CHECKED")

    def test_optional_uncovered_is_visible(self):
        manifest = copy.deepcopy(self.profile)
        manifest["mappings"].append({"id": "coverage", "kind": "uncovered", "required": False,
                                     "reason": "No field contract"})
        report = self.run_audit(manifest=manifest)
        self.assertEqual(report["summary"], {"UNCHANGED": 3, "NOT_CHECKED": 1})
        self.assertTrue(report["required_checks_passed"])

    def test_pattern_scanner_preserves_overlap_and_wildcard_newlines(self):
        data = binary()
        put(data, 0x10c0, bytes.fromhex("aa 0a aa 0a aa"))
        self.assertEqual(scan(PEImage(data), "aa ?? aa"), [0x10c0, 0x10c2])
        put(data, 0x23f0, bytes.fromhex("aa 0a aa"))
        self.assertEqual(scan(PEImage(data), "aa ?? aa"), [0x10c0, 0x10c2])

    def test_empty_and_all_wildcard_signatures_rejected(self):
        for pattern in ["", "?? ??", "xx", "aa ?1"]:
            with self.subTest(pattern=pattern), self.assertRaises(ValueError):
                compile_pattern(pattern)

    def test_partial_unwind_record_does_not_hide_later_field_evidence(self):
        data = binary()
        struct.pack_into("<I", data, 0xc04, 0x1017)
        manifest = profile(data)
        report = self.run_audit(data, manifest)
        self.assertTrue(report["required_checks_passed"])
        self.assertEqual(self.rows(report)["game/field"]["status"], "UNCHANGED")

    def test_legacy_export_ranges_are_decoded_and_size_checked(self):
        image = PEImage(self.data)
        instructions = decode(image, 0x1010, 0x30)
        assembly = self.root / "function.asm"
        assembly.write_text("".join(f"{i.address:#x}\t{i.mnemonic}\n" for i in instructions))
        function = {"rva": "0x1010", "body_address_count": 0x30}
        self.assertEqual(legacy_ranges(image, assembly, function),
                         [{"start_rva": "0x1010", "end_rva_inclusive": "0x103f"}])
        function["body_address_count"] += 1
        with self.assertRaisesRegex(ValueError, "body size"):
            legacy_ranges(image, assembly, function)

    def test_internal_branch_displacement_is_preserved(self):
        data = binary()
        put(data, 0x1010, b"\x75\x05" + b"\x90" * 46)
        expected = fingerprint(PEImage(data), 0x1010, [{"offset": 0, "size": 48}])
        put(data, 0x1010, b"\x75\x06")
        self.assertNotEqual(expected, fingerprint(PEImage(data), 0x1010, [{"offset": 0, "size": 48}]))

    def test_direct_caller_can_anchor_a_leaf_without_unwind_metadata(self):
        manifest = copy.deepcopy(self.profile)
        image = PEImage(self.data)
        instructions = decode(image, 0x1010, 0x30)
        leaf = make_function(image, 0x1080, [{"offset": 0, "size": 32}], [])
        leaf.update(id="game/leaf", binary="game", required=True,
                    callers=[{"parent": "game/function", "pattern": pattern_for(instructions[:5]), "instruction": 2}])
        manifest["mappings"].append(leaf)
        data = binary()
        # Keep only the caller's unwind record; the direct call is independent entry evidence.
        struct.pack_into("<I", data, 0x98 + 112 + 3 * 8 + 4, 12)
        rows = self.rows(self.run_audit(data, manifest))
        self.assertEqual(rows["game/leaf"]["status"], "UNCHANGED")
        self.assertEqual(rows["game/leaf"]["current"]["entry_evidence"], "no_unwind_range")

    def test_checked_in_profile_has_valid_stable_dependencies(self):
        path = Path(__file__).resolve().parents[1] / "artifacts/build-25492057/compatibility-profile.json"
        if not path.is_file():
            self.skipTest("Baseline profile not generated yet")
        manifest = json.loads(path.read_text())
        validate_profile(manifest)
        self.assertFalse(manifest["runtime_verified"])
        self.assertTrue(any(m["kind"] == "operand" for m in manifest["mappings"]))

    def test_profile_validation_rejects_duplicate_and_cross_binary_ids(self):
        manifest = copy.deepcopy(self.profile)
        manifest["mappings"].append(copy.deepcopy(manifest["mappings"][0]))
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            validate_profile(manifest)
        manifest = copy.deepcopy(self.profile)
        manifest["mappings"][1]["parent"] = "missing"
        with self.assertRaisesRegex(ValueError, "operand parent"):
            validate_profile(manifest)

    def test_malformed_enum_is_unresolved_not_a_crash(self):
        data = binary()
        put(data, 0x2300, struct.pack("<Q", 0xffffffffffffffff))
        self.assertEqual(self.rows(self.run_audit(data))["game/enum"]["status"], "UNRESOLVED")

    def test_cli_exit_codes_reports_and_strict_coverage(self):
        manifest = copy.deepcopy(self.profile)
        manifest["mappings"].append({"id": "coverage", "kind": "uncovered", "required": False, "reason": "Unmapped"})
        source = self.root / "profile.json"
        source.write_text(json.dumps(manifest))
        (self.root / "game.exe").write_bytes(self.data)
        command = [sys.executable, str(Path(__file__).resolve().parents[1] / "scripts/check_update.py"),
                   "--profile", str(source), "--binaries-dir", str(self.root), "--build-id", "101",
                   "--output-dir", str(self.root / "output")]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertTrue((self.root / "output/report.md").is_file())
        self.assertIn("game/function:UNCHANGED", (self.root / "output/game.candidates.tsv").read_text())
        result = subprocess.run(command + ["--strict"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stderr + result.stdout)
        report = json.loads((self.root / "output/report.json").read_text())
        self.assertEqual(report["exit_code"], 1)
        self.assertFalse(report["runtime_allowed"])
        source.write_text('{"schema_version": 99}')
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        source.write_text('[]')
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
