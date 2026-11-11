#!/usr/bin/env python3
"""Parse signed TU1 category counts and reject out-of-bounds or swapped metadata."""
from pathlib import Path
import struct
import zlib
import sys
import unittest
sys.path.insert(0, str(Path(__file__).parent))
from audit_hulk_tu1_resource_index import parse_index, audit, decode_tu1_resource, compare_tu1_resource_payloads

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


def wrapped_tu1(*segments: bytes) -> bytes:
    """Generate realistic header-only ZIP entries without a central directory."""
    total = sum(len(s) for s in segments)
    out = bytearray(struct.pack(">II", total, 0x10000))
    for plain in segments:
        assert 0 < len(plain) <= 65536
        deflater = zlib.compressobj(wbits=-15)
        compressed = deflater.compress(plain) + deflater.flush()
        local = (struct.pack("<IHHHHHIIIHH", 0x04034B50, 0, 0, 8, 0, 0,
                             0, len(compressed), len(plain), 0, 0) + compressed)
        out.extend(struct.pack(">I", len(local)))
        out.extend(local)
    return bytes(out)


class TestHulkTu1RealDeltaDecoder(unittest.TestCase):
    def test_single_chunk_and_real_64k_boundary(self):
        data = b"city_zone_6_5__lod\0" + b"x" * 70000
        wrapped = wrapped_tu1(data[:65536], data[65536:])
        self.assertEqual(decode_tu1_resource(wrapped), data)
        self.assertEqual(decode_tu1_resource(wrapped_tu1(b"a")), b"a")

    def test_rejects_short_corrupt_and_bomb_payloads(self):
        valid = wrapped_tu1(b"x" * 100)
        broken = [
            b"", valid[:-1], valid + b"\0",
            valid[:4] + b"\0\0\0\1" + valid[8:],
            valid[:8] + b"\xff\xff\xff\xff" + valid[12:],
            valid[:12] + b"BAD!" + valid[16:],
        ]
        for blob in broken:
            with self.subTest(size=len(blob)):
                with self.assertRaises(ValueError):
                    decode_tu1_resource(blob)
        with self.assertRaises(ValueError):
            decode_tu1_resource(valid, max_output=50)

    def test_correct_lod_change_and_existing_same_resource(self):
        name = b"QuickDatas\0"
        key = 0x14CD4AAC
        old = b"city_zone_6_5__lod\0original"
        new = b"city_zone_6_5__lod\0patched!"
        base = sample_index(name, 1, key, offset=4, size=len(old))
        delta = sample_index(name, -1, key, offset=4,
                             size=len(wrapped_tu1(new)))
        report = compare_tu1_resource_payloads(
            base, delta, {"QuickDatas": b"\0" * 4 + old},
            {"QuickDatas": b"\0" * 4 + wrapped_tu1(new)})
        group = report["groups"]["QuickDatas"]
        self.assertEqual(group["decoded_different_from_usa"], 1)
        self.assertEqual(group["decoded_byte_identical_to_usa"], 0)
        self.assertEqual(group["city_low_lod_records"][0]["resource_name"],
                         "city_zone_6_5__lod")
        self.assertFalse(report["usa_runtime_overlay_enabled"])
        self.assertFalse(report["water_logic_fix_installed"])
        identical = compare_tu1_resource_payloads(
            base, delta, {"QuickDatas": b"\0" * 4 + new},
            {"QuickDatas": b"\0" * 4 + wrapped_tu1(new)})
        self.assertEqual(identical["total_equal"], 1)
        missing = compare_tu1_resource_payloads(
            base, delta, {}, {"QuickDatas": b"\0" * 4 + wrapped_tu1(new)})
        self.assertEqual(missing["unexamined_groups"], ["QuickDatas"])
        self.assertEqual(missing["total_different"], 0)


if __name__ == "__main__":
    unittest.main()
