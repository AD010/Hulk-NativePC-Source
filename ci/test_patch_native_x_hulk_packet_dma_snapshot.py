#!/usr/bin/env python3
"""Regression tests: snapshot only packet-time DMA indices for Hulk NativeX."""
import unittest
from patch_native_x_hulk_packet_dma_snapshot import (
    ANCHOR, ADDITION, CALL_ANCHOR, CALL_NEW, patch_text
)

class DmaSnapshotTests(unittest.TestCase):
    def test_atomic_idempotent(self):
        original = ANCHOR + "\n" + CALL_ANCHOR
        updated,changed=patch_text(original)
        self.assertTrue(changed)
        self.assertEqual(patch_text(updated),(updated,False))
        with self.assertRaises(ValueError):patch_text(ADDITION+CALL_ANCHOR)
        with self.assertRaises(ValueError):patch_text("unrecognized revision")

    def test_no_fabricated_index_data(self):
        for token in (
            "tracker.LastWrite(rg::XE_GPU_REG_VGT_DRAW_INITIATOR",
            "tracker.LastWrite(rg::XE_GPU_REG_VGT_DMA_BASE",
            "tracker.LastWrite(rg::XE_GPU_REG_VGT_DMA_SIZE",
            "std::min(requested_indices, available_indices)",
            "bytes > kMaxSnapshotBytes",
            "uint64_t(physical) + bytes > kGuestPhysicalBytes",
            "c.copy_count >= c.copies.size()",
            "c.data.insert(c.data.end(), source, source + bytes)",
            "[HULK/PM4-DMA-SNAPSHOT]"
        ):
            self.assertIn(token, ADDITION)
        self.assertIn("if (packet_surfaces) CapturePacketDmaIndices",CALL_NEW)
        self.assertNotIn("memset",ADDITION)

if __name__=="__main__":unittest.main()
