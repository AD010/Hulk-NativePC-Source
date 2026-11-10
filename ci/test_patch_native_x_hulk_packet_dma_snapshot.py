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
            "if (!requested_indices || requested_indices > available_indices)",
            "return reject(\"DMA index count missing/short\")",
            "return reject(\"DMA address/length invalid\")",
            "bytes > kMaxSnapshotBytes",
            "uint64_t(physical) + bytes > kGuestPhysicalBytes",
            "c.copy_count >= c.copies.size()",
            "c.data.insert(c.data.end(), source, source + bytes)",
            "[HULK/PM4-DMA-SNAPSHOT]"
        ):
            self.assertIn(token, ADDITION)
        self.assertIn("if (packet_surfaces && !CapturePacketDmaIndices(tracker, base, c)) return;",CALL_NEW)
        self.assertIn("bool CapturePacketDmaIndices(", ADDITION)
        self.assertIn("const uint32_t indices = requested_indices;", ADDITION)
        self.assertIn("[HULK/PM4-DMA-REJECT]", ADDITION)
        self.assertIn("return true;  // Auto-indexed packet", ADDITION)
        self.assertNotIn("std::min(requested_indices, available_indices)", ADDITION)
        self.assertNotIn("memset",ADDITION)

if __name__=="__main__":unittest.main()
