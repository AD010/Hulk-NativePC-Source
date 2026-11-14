#!/usr/bin/env python3
"""Synthetic and source-pinning tests: no copyrighted guest binaries."""
import unittest
from patch_native_x_hulk_pm4_gap import (
    HEADER_ANCHOR,HEADER_NEW,RESET_ANCHOR,RESET_NEW,
    ENTER_ANCHOR,ENTER_NEW,LEAVE_ANCHOR,LEAVE_NEW,
    EMPTY_LEAVE_ANCHOR,EMPTY_LEAVE_NEW,patch_cpp,patch_header)

class TestPm4Gap(unittest.TestCase):
    def test_header(self):
        got,changed=patch_header(HEADER_ANCHOR)
        self.assertTrue(changed)
        self.assertIn("last_hulk_segment_valid_",got)
        self.assertEqual(patch_header(got),(got,False))
    def test_cpp(self):
        before="\n".join([RESET_ANCHOR,ENTER_ANCHOR,LEAVE_ANCHOR,EMPTY_LEAVE_ANCHOR])
        after,changed=patch_cpp(before)
        self.assertTrue(changed)
        self.assertIn("ScanPackets(base, last_hulk_segment_end_, p.begin",after)
        self.assertIn("Gather(p, call, false, base);",after)
        self.assertIn("scan_.complete",after)
        self.assertIn("<= 0x40000u",after)
        self.assertIn("[HULK/STATE-GAP]",after)
        self.assertIn("HULK/PM4-GAP-CONSUME-EMPTY",after)
        self.assertIn("last_hulk_segment_end_ = p.begin;",after)
        self.assertIn("last_hulk_segment_valid_ = false;",after)
        self.assertEqual(patch_cpp(after),(after,False))
    def test_fail_closed(self):
        with self.assertRaises(ValueError):
            patch_cpp(RESET_NEW+"\n"+ENTER_ANCHOR+"\n"+LEAVE_ANCHOR)
        with self.assertRaises(ValueError):
            patch_header("unrelated SDK")

if __name__=="__main__":
    unittest.main()
