#!/usr/bin/env python3
"""Parse signed TU1 category counts and reject out-of-bounds or swapped metadata."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).parent))
from audit_hulk_tu1_resource_index import parse_index, audit

def sample_index(name: bytes, count: int, h: int, offset: int = 4, size: int = 17):
    header_bytes = 4 + 3*4
    name_pos = header_bytes
    table_pos = name_pos + len(name)
    end_pos = table_pos + 20
    return (struct.pack(">4I", 2, name_pos, table_pos, end_pos)
            + name + struct.pack(">i4I", count, h, offset, size, 0))

class TestHulkTu1Audit(unittest.TestCase):
    def test_signed_entries(self):
        a = sample_index(b"Objects\x00", -1, 0x23450001)
        z = parse_index(a)["Objects"]
        self.assertEqual(z["signed_count"], -1)
        self.assertEqual(z["entries"]["23450001"]["size"], 17)
    def test_bounds_check_and_us_hash(self):
        base = sample_index(b"Objects\x00", 1, 0x23450001)
        delta = sample_index(b"Objects\x00", -1, 0x23450001)
        report = audit(base, delta, {"Objects": bytes(25)})
        self.assertEqual(report["hashes_absent_from_us"], 0)
        self.assertTrue(report["all_delta_ranges_in_bounds"])
        self.assertFalse(report["code_patch_compatible_with_us"])
        report2 = audit(base, delta, {"Objects": bytes(12)})
        self.assertFalse(report2["all_delta_ranges_in_bounds"])
    def test_zero_entry_base_category(self):
        hdr = struct.pack(">4I", 2, 16, 24, 28) + b"Actions\x00" + struct.pack(">i", 0)
        self.assertEqual(parse_index(hdr)["Actions"]["entries"], {})

    def test_bad_index_refused(self):
        with self.assertRaises(ValueError):
            parse_index(bytes(20))
        with self.assertRaises(ValueError):
            parse_index(sample_index(b"Objects\x00", 2, 0x23450001))

if __name__ == "__main__":
    unittest.main()
