"""Synthetic PE tests: no game files or third-party modules required."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from scan_pe import PEImage


def fixture():
    data = bytearray(0xE00)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x8664, 3)
    struct.pack_into("<H", data, 0x94, 0xF0)
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<Q", data, optional + 24, 0x140000000)
    struct.pack_into("<I", data, optional + 56, 0x4000)
    struct.pack_into("<I", data, optional + 108, 16)
    struct.pack_into("<II", data, optional + 112 + 3 * 8, 0x3000, 24)
    sections = [(b".text", 0x1000, 0x200, 0x400, 0x20000020),
                (b".rdata", 0x2000, 0x600, 0x600, 0x40000040),
                (b".pdata", 0x3000, 0x200, 0xC00, 0x40000040)]
    for i, (name, rva, raw_size, raw, flags) in enumerate(sections):
        pos = optional + 0xF0 + i * 40
        data[pos:pos + len(name)] = name
        struct.pack_into("<IIII", data, pos + 8, 0x1000, rva, raw_size, raw)
        struct.pack_into("<I", data, pos + 36, flags)
    struct.pack_into("<6I", data, 0xC00, 0x1010, 0x1040, 0x2200,
                     0x1080, 0x10A0, 0x2210)
    name = b".?AVExample@@\0"
    data[0x610:0x610 + len(name)] = name
    struct.pack_into("<6I", data, 0x680, 1, 0, 0, 0x2000, 0x20A0, 0x2080)
    struct.pack_into("<4I", data, 0x6A0, 0, 0, 1, 0x20C0)
    struct.pack_into("<I", data, 0x6C0, 0x20D0)
    struct.pack_into("<IIiiiI", data, 0x6D0, 0x2000, 0, 0, -1, 0, 0)
    struct.pack_into("<3Q", data, 0x700, 0x140002080, 0x140001010, 0x140001080)
    return data


class PEImageTests(unittest.TestCase):
    def test_rva_is_not_file_offset(self):
        pe = PEImage(fixture())
        self.assertEqual(pe.offset(0x1010), 0x410)
        self.assertEqual(pe.rva(0x410), 0x1010)
        self.assertTrue(pe.executable(0x1010))
        self.assertFalse(pe.executable(0x2010))

    def test_unbacked_memory_and_cross_section_reads_rejected(self):
        pe = PEImage(fixture())
        for rva, size in [(0x1200, 1), (0x11FF, 2), (0x2600, 1), (-1, 1)]:
            with self.subTest(rva=rva), self.assertRaises(ValueError):
                pe.offset(rva, size)

    def test_unwind_boundary_does_not_accept_interior_or_end(self):
        pe = PEImage(fixture())
        self.assertEqual(pe.coverage(0x1010)["status"], "unwind_start")
        self.assertEqual(pe.coverage(0x1011)["offset_into_range"], 1)
        self.assertEqual(pe.coverage(0x1040)["status"], "no_unwind_range")

    def test_rtti_self_reference_and_vtable(self):
        result = PEImage(fixture()).rtti(".?AVExample@@")
        locator = result[0]["locators"][0]
        self.assertEqual(result[0]["type_descriptor_rva"], "0x2000")
        self.assertEqual(locator["bases"][0]["pmd"]["vbtable_pointer_displacement"], -1)
        table = locator["vtables"][0]
        self.assertEqual(table["rva"], "0x2108")
        self.assertEqual([m["rva"] for m in table["methods"]], ["0x1010", "0x1080"])
        corrupt = fixture()
        struct.pack_into("<I", corrupt, 0x694, 0x2081)
        self.assertEqual(PEImage(corrupt).rtti(".?AVExample@@")[0]["locators"], [])

    def test_truncated_section_rejected(self):
        with self.assertRaises(ValueError):
            PEImage(fixture()[:-1])

    def test_exact_literals_exclude_suffixes(self):
        data = fixture()
        value = b"\0fire_mode\0not_fire_mode\0"
        data[0x800:0x800 + len(value)] = value
        self.assertEqual(PEImage(data).literal("fire_mode"), ["0x2201"])


if __name__ == "__main__":
    unittest.main()
