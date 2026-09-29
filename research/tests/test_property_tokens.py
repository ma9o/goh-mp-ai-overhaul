"""Reject misleading stub-shaped data while accepting zero-filled globals."""
import struct
import tempfile
import unittest
from pathlib import Path

from test_scan_pe import fixture
from catalog_property_tokens import catalog


def token_fixture(*, registry=0x2700, entry=0x2720, target=0x1010):
    data = fixture()
    # Give .rdata a writable virtual tail; globals there have no file bytes.
    struct.pack_into("<I", data, 0x98 + 0xF0 + 40 + 36, 0xC0000040)
    data[0x800:0x806] = b"Range\0"
    code = (b"\x41\xb9" + struct.pack("<I", 23)
            + b"\x4c\x8d\x05" + struct.pack("<i", 0x2200 - 0x105D)
            + b"\x48\x8d\x15" + struct.pack("<i", registry - 0x1064)
            + b"\x48\x8d\x0d" + struct.pack("<i", entry - 0x106B)
            + b"\xe9" + struct.pack("<i", target - 0x1070))
    data[0x450:0x470] = code
    return data


class PropertyTokenTests(unittest.TestCase):
    def scan(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "synthetic.exe"
            path.write_bytes(data)
            return catalog(path, "test", 0x1010)["registrations"]

    def test_unbacked_globals_and_backward_call_are_valid(self):
        rows = self.scan(token_fixture())
        self.assertEqual(len(rows), 1)
        self.assertEqual((rows[0]["token"], rows[0]["value"]), ("Range", 23))
        self.assertEqual(rows[0]["registry_rva"], "0x2700")

    def test_wrong_registrar_and_out_of_image_globals_rejected(self):
        for kwargs in [{"target": 0x1080}, {"registry": 0x4000}, {"entry": 0x2FF0}]:
            with self.subTest(kwargs=kwargs):
                self.assertEqual(self.scan(token_fixture(**kwargs)), [])

    def test_stub_in_non_executable_data_is_rejected(self):
        data = token_fixture()
        # Move identical bytes to data and remove executable copy.
        data[0x900:0x920] = data[0x450:0x470]
        data[0x450:0x470] = bytes(32)
        self.assertEqual(self.scan(data), [])


if __name__ == "__main__":
    unittest.main()
