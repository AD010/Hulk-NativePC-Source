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
            "tracker.HulkRegisterAtDraw(rg::XE_GPU_REG_VGT_DMA_BASE",
            "tracker.HulkRegisterAtDraw(rg::XE_GPU_REG_VGT_DMA_SIZE",
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


    def test_persistent_dma_register_values(self):
        # Packet-local LastWrite cannot observe the last valid DMA BASE/SIZE
        # before a preceding synthetic draw's scan_.Clear(). The effective GPU
        # register image must supply these persisted values, while the actual
        # draw initiator is still required to belong to the current packet.
        self.assertIn("HULK/PM4-PERSISTENT-DMA", ADDITION)
        self.assertIn("tracker.LastWrite(rg::XE_GPU_REG_VGT_DRAW_INITIATOR", ADDITION)
        self.assertIn("dma_base = tracker.HulkRegisterAtDraw(", ADDITION)
        self.assertIn("dma_size = tracker.HulkRegisterAtDraw(", ADDITION)
        self.assertNotIn("tracker.LastWrite(rg::XE_GPU_REG_VGT_DMA_BASE", ADDITION)
        self.assertNotIn("tracker.LastWrite(rg::XE_GPU_REG_VGT_DMA_SIZE", ADDITION)
        # Legal inherited binding: earlier packet wrote base/size, current
        # packet updates only initiator; request must stay within limits.
        def accepted(initiator, effective_base, effective_size):
            source = (initiator >> 6) & 3
            if source: return True
            requested = initiator >> 16
            available = effective_size & 0xFFFFFF
            stride = 4 if (initiator >> 11) & 1 else 2
            physical = effective_base & 0x1FFFFFFF
            return (bool(requested) and requested <= available and
                    requested * stride <= (1 << 20) and
                    physical + requested * stride <= 0x20000000)
        self.assertTrue(accepted((24 << 16) | 4, 0x1721D700, 24))
        self.assertFalse(accepted((25 << 16) | 4, 0x1721D700, 24))
        self.assertFalse(accepted((24 << 16) | 4, 0x1721D700, 0))
        self.assertFalse(accepted((0 << 16) | 4, 0x1721D700, 24))
        self.assertFalse(accepted((24 << 16) | 4, 0x1FFFFFF0, 24))

if __name__=="__main__":unittest.main()
