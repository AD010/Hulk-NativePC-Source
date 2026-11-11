#!/usr/bin/env python3
"""Offline Clear/ClearF AOT scanner unit tests, no game code or shader bytes."""
import struct
import tempfile
import unittest
from pathlib import Path
from audit_hulk_usa_clear_aot import direct_calls,read_mapping,read_text_section


class HulkUsaClearAotAuditTests(unittest.TestCase):
    def test_rel32_call_target_no_false_positive(self):
        start=0x140010000
        target=0x140011000
        buf=b"\xe8"+struct.pack("<i",target-start-5)+b"\x90\x90\xe8\0\0\0\0"
        self.assertEqual(direct_calls(buf,start,target),[start])
        self.assertEqual(direct_calls(buf,start,target+1),[])

    def test_reject_small_mapping_and_duplicates(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/"map.csv"
            p.write_text("guest_address,native_entry\n0x82888D68,0x1410ECD70\n",encoding="utf-8")
            with self.assertRaisesRegex(ValueError,"99,269"):
                read_mapping(p)
            p.write_text("guest_address,native_entry\n0x82888D68,0x1410ECD70\n"
                         "0x82888D68,0x1410ECD70\n",encoding="utf-8")
            with self.assertRaisesRegex(ValueError,"Duplicate guest"):
                read_mapping(p)

    def test_reject_non_pe_data(self):
        for data in (b"wrong",b"MZ"+bytes(300)):
            with self.assertRaises(ValueError):
                read_text_section(data)


if __name__=="__main__":
    unittest.main()
