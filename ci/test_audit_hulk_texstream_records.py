#!/usr/bin/env python3
"""Synthetic diagnostics of 56-byte candidate TexStreams resource prefix."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).parent))
import audit_hulk_texstream_records as t

class Tests(unittest.TestCase):
    def test_ten_original_style_record_sizes_and_unverified_region(self):
        sizes = [98360,8248,360504,10485816,98360,65592,360504,180280,5242936,8248]
        assert len(sizes) == 10
        self.assertTrue(all((x-56) % 4096 == 0 for x in sizes))
        self.assertFalse(all((x-64) % 4096 == 0 for x in sizes))
    def test_exact_headers_and_identity(self):
        with tempfile.TemporaryDirectory() as dirname:
            directory=Path(dirname)
            record=bytes(range(56)) + bytes([0xAA])*4096
            p=directory/"building_n_ABCD1234.bin"
            p.write_bytes(record)
            (directory/"manifest.txt").write_text(
                "Original Xbox 360 Hulk TexStreams\n"
                "Source file size: 1330443104\n"
                "building_n hash=0xABCD1234 offset=200 size=4152\n",
                encoding="utf-8")
            report=t.audit(directory)
            self.assertEqual(report["record_count"],1)
            self.assertFalse(report["index_region_confirmed"])
            self.assertTrue(report["all_records_have_possible_56_byte_prefix_and_4k_body"])
            self.assertEqual(report["records"][0]["sha256"],
                             hashlib.sha256(record).hexdigest())
            self.assertEqual(report["records"][0]["first_56_be_le_words"][0]["be"],
                             0x00010203)
            self.assertEqual(report["records"][0]["first_56_be_le_words"][0]["le"],
                             0x03020100)
            self.assertFalse(report["xenos_gpu_format_verified"])
            self.assertFalse(report["live_draw_sampler_binding_verified"])
            p.write_bytes(record[:-1])
            with self.assertRaisesRegex(ValueError,"length differs"):
                t.audit(directory)
    def test_missing_and_aliases_rejected(self):
        with tempfile.TemporaryDirectory() as dirname:
            directory=Path(dirname)
            (directory/"manifest.txt").write_text(
                "Source file size: 10000\n"
                "tex_n hash=0xDEADBEEF offset=100 size=56\n")
            with self.assertRaisesRegex(ValueError,"sample set mismatch"):
                t.audit(directory)
            (directory/"tex_n_DEADBEEF.bin").write_bytes(bytes(56))
            report=t.audit(directory,index_sha256=t.EXPECTED_US_INDEX_SHA)
            self.assertTrue(report["original_index_is_sha256_pinned_usa"])
            with self.assertRaisesRegex(ValueError,"Invalid original INDEX"):
                t.audit(directory,index_sha256="wrong")

if __name__ == "__main__":
    unittest.main()
