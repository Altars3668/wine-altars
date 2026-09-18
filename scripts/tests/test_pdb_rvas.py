import importlib.util
import pathlib
import struct
import unittest


spec = importlib.util.spec_from_file_location(
    "pdb_rvas", pathlib.Path(__file__).resolve().parents[1] / "pdb-rvas.py")
pdb_rvas = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pdb_rvas)


class AddressMapTests(unittest.TestCase):
    def test_no_omap_uses_pe_sections(self):
        dbi = bytes(64)
        sections, mapping = pdb_rvas.address_map(dbi, lambda _: b"", [0x1000, 0x2000])
        self.assertEqual(sections, [0x1000, 0x2000])
        self.assertEqual(mapping, [])

    def test_omap_uses_original_sections(self):
        dbi = bytearray(64 + 6 + 22)
        struct.pack_into("<I", dbi, 24, 6)
        struct.pack_into("<I", dbi, 48, 22)
        indexes = [0xffff] * 11
        indexes[4], indexes[10] = 8, 6
        struct.pack_into("<11H", dbi, 70, *indexes)
        headers = bytearray(80)
        struct.pack_into("<I", headers, 12, 0x1000)
        struct.pack_into("<I", headers, 52, 0xb00000)
        omap = struct.pack("<6I", 0x1000, 0x1000, 0xbde000, 0xb1a7f0, 0xbde100, 0)
        streams = {6: bytes(headers), 8: omap}
        sections, mapping = pdb_rvas.address_map(bytes(dbi), streams.__getitem__, [0x1000, 0xa00000])
        self.assertEqual(sections, [0x1000, 0xb00000])
        self.assertEqual(pdb_rvas.remap_rva(0xbde070, mapping), 0xb1a860)
        self.assertIsNone(pdb_rvas.remap_rva(0xbde100, mapping))
        self.assertIsNone(pdb_rvas.remap_rva(0x100, mapping))

    def test_missing_original_sections_is_not_silently_accepted(self):
        dbi = bytearray(86)
        struct.pack_into("<I", dbi, 48, 22)
        indexes = [0xffff] * 11
        indexes[4] = 8
        struct.pack_into("<11H", dbi, 64, *indexes)
        with self.assertRaises(ValueError):
            pdb_rvas.address_map(bytes(dbi), lambda _: struct.pack("<II", 0x1000, 0x2000), [0x1000])

    def test_empty_mapping_is_identity(self):
        self.assertEqual(pdb_rvas.remap_rva(0x1234, []), 0x1234)

    def test_truncated_dbi_is_rejected(self):
        with self.assertRaises(ValueError):
            pdb_rvas.address_map(bytes(20), lambda _: b"", [0x1000])


if __name__ == "__main__":
    unittest.main()
