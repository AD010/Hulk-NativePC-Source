#!/usr/bin/env python3
"""Synthetic PM4 first-pass SET/LOAD ownership tests."""
import unittest
from patch_native_x_hulk_gap_register_snapshot import (
    change,HEAD_OLD,HEAD_NEW,DRAW_OLD,DRAW_NEW,
    TAIL_OLD,TAIL_NEW,DIAG_OLD,DIAG_NEW,
    FLOAT_SCAN_OLD,FLOAT_SCAN_NEW,FLOAT_GATHER_OLD,FLOAT_GATHER_NEW
)

def restore(first, second):
    # Each entry is (source_type, register, value) where source_type=0/1.
    if len(first)<len(second):return None
    for i,(kind,reg,_) in enumerate(second):
        if (kind,reg)!=first[i][:2]:return None
    return [(kind,reg,first[i][2]) for i,(kind,reg,_) in enumerate(second)]

class Pm4RegisterLifetimeTests(unittest.TestCase):
    def test_atomic_patch_and_idempotence(self):
        src=("HULK/PM4-PRESCAN-SHADER-SNAPSHOT\n"+
             HEAD_OLD+DRAW_OLD+TAIL_OLD+DIAG_OLD+
             FLOAT_SCAN_OLD+FLOAT_GATHER_OLD)
        fixed,did=change(src)
        self.assertTrue(did)
        self.assertEqual(change(fixed),(fixed,False))
        for fragment in (HEAD_NEW,DRAW_NEW,TAIL_NEW,DIAG_NEW,
                         FLOAT_SCAN_NEW,FLOAT_GATHER_NEW):
            self.assertIn(fragment,fixed)
        self.assertIn("hulk_register_cursor == hulk_prescan_register_order.size()",fixed)
        self.assertIn("prior.first != now.first",fixed)
        for wrong in ("upstream drift",HEAD_NEW+DRAW_OLD+TAIL_OLD):
            with self.assertRaises(ValueError):change(wrong)

    def test_first_scan_owns_values_and_order(self):
        original=[(0,0x4806,0xDEADBEEF),(1,0x4807,0xAABBCCDD),
                  (0,0x2C10,0x00010004)]
        reused=[(0,0x4806,0x44A00000),(1,0x4807,0x44340000),
                (0,0x2C10,0xDEAD0000)]
        self.assertEqual(restore(original,reused),original)
        self.assertIsNone(restore(original,reused+[(0,0x100,7)]))
        self.assertIsNone(restore(original,[(1,0x4806,9)]))
        self.assertIsNone(restore(original,[(0,0x4807,9)]))
        self.assertEqual(restore(original,reused[:2]),original[:2])

    def test_xdk_draw_keeps_packet_float_constants(self):
        source=("HULK/PM4-PRESCAN-SHADER-SNAPSHOT\\n"+
                HEAD_OLD+DRAW_OLD+TAIL_OLD+DIAG_OLD+
                FLOAT_SCAN_OLD+FLOAT_GATHER_OLD)
        actual, did = change(source)
        self.assertTrue(did)
        self.assertIn("ScanPackets(base, p.scan_from, end, scan_, 0, false, false)", actual)
        self.assertIn("value != shadow(hulk_shadow_byte)", actual)
        self.assertIn("d.ps_float_constants", actual)
        self.assertIn("d.vs_float_constants", actual)
        self.assertNotIn(FLOAT_SCAN_OLD, actual)
        self.assertNotIn(FLOAT_GATHER_OLD, actual)
        # XDK packet VS c21.x differs from the later shadow in #197.
        shadow=0xBF7F67F5
        packet=0xBF7FCBBA
        self.assertNotEqual(shadow, packet)
        resolved = packet if packet != shadow else shadow
        self.assertEqual(resolved, packet)
        with self.assertRaises(ValueError):
            change(source.replace(FLOAT_SCAN_OLD, "unreviewed scanner"))

    def test_do_not_invent_texture_or_index_values(self):
        self.assertNotIn("skip_fetch",HEAD_NEW)
        self.assertNotIn("0x44A00000",HEAD_NEW)
        self.assertNotIn("0x44340000",HEAD_NEW)
        self.assertNotIn("std::memset",HEAD_NEW)

if __name__=="__main__":
    unittest.main()
