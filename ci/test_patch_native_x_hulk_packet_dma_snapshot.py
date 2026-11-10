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
            "HULK/PM4-DMA-ADDRESS-PARITY",
            "const uint32_t physical = dma_base & ~(stride - 1u)",
            "c.copies[c.copy_count++] = {physical",
            "[HULK/PM4-DMA-SNAPSHOT]"
        ):
            self.assertIn(token, ADDITION)
        self.assertIn("if (packet_surfaces && !CapturePacketDmaIndices(tracker, base, c)) {", CALL_NEW)
        self.assertIn("c.type = Command::Type::kNone;", CALL_NEW)
        self.assertNotIn("base, c)) return;", CALL_NEW)
        self.assertIn("bool CapturePacketDmaIndices(", ADDITION)
        self.assertIn("const uint32_t indices = requested_indices;", ADDITION)
        self.assertIn("[HULK/PM4-DMA-REJECT]", ADDITION)
        self.assertIn("return true;  // Xenos kAutoIndex", ADDITION)
        self.assertIn("HULK/PM4-PRIMITIVE-SOURCE", ADDITION)
        self.assertIn('return reject("invalid Xenos primitive type")', ADDITION)
        self.assertIn('return reject("PM4 immediate/reserved indices not captured")', ADDITION)
        self.assertNotIn("std::min(requested_indices, available_indices)", ADDITION)
        self.assertNotIn("memset",ADDITION)


    def test_rejected_dma_draw_must_not_publish_as_draw(self):
        # NativeX upstream Capture::~Capture publishes the command even on
        # early return. Its Execute path applies the register record first,
        # then ignores Command::Type::kNone.
        self.assertIn("HULK/PM4-DMA-CANCEL", CALL_NEW)
        self.assertIn("Capture::~Capture()", CALL_NEW)
        self.assertIn("c.type = Command::Type::kNone;", CALL_NEW)
        self.assertIn("return;", CALL_NEW)
        def execute(command_type, record, old_state):
            state = record if record is not None else old_state
            rendered = command_type == "kDraw"
            return state, rendered
        self.assertEqual(execute("kNone", 7, 0), (7, False))
        self.assertEqual(execute("kDraw", 7, 0), (7, True))

    def test_xenos_primitive_and_index_source_classification(self):
        # Cross-checked against Xenia xenos::PrimitiveType and SourceSelect:
        # 0: no primitive, 1..8 and 12..22 are defined primitive values.
        # Source 0=DMA, 1=immediate, 2=auto, 3=reserved.
        # Immediate source requires copying packet-embedded indices, which
        # this path does NOT do; passing it through uses stale GPU DMA state.
        def decision(initiator):
            primitive = initiator & 0x3F
            source_select = (initiator >> 6) & 3
            if not (1 <= primitive <= 8 or 12 <= primitive <= 22):
                return "reject_primitive"
            if source_select == 2:
                return "auto_index"
            if source_select != 0:
                return "reject_uncaptured_source"
            return "dma_snapshot"
        self.assertEqual(decision((180 << 16) | 4), "dma_snapshot")
        self.assertEqual(decision((180 << 16) | (2 << 6) | 4), "auto_index")
        self.assertEqual(decision((180 << 16) | (1 << 6) | 4),
                         "reject_uncaptured_source")
        self.assertEqual(decision((180 << 16) | (3 << 6) | 4),
                         "reject_uncaptured_source")
        self.assertEqual(decision((180 << 16) | 49), "reject_primitive")
        for primitive in (0, 9, 10, 11, 23, 31, 49, 63):
            self.assertEqual(decision(primitive), "reject_primitive")
        for primitive in (1, 2, 3, 4, 5, 6, 7, 8, 12, 13,
                          14, 15, 16, 17, 18, 19, 20, 21, 22):
            self.assertEqual(decision(primitive), "dma_snapshot")

    def test_exact_sdk_dma_index_address_parity(self):
        # Verified from bundled ReXGlue SDK
        # src/graphics/primitive_processor.cpp:
        # guest_index_base = regs[VGT_DMA_BASE] & ~(stride - 1)
        # SharedMemory::kBufferSize = 1 << 29.
        self.assertIn("HULK/PM4-DMA-ADDRESS-PARITY", ADDITION)
        self.assertIn("const uint32_t physical = dma_base & ~(stride - 1u)", ADDITION)
        self.assertIn("{physical, uint32_t(c.data.size()), uint32_t(bytes)}", ADDITION)
        self.assertNotIn("const uint32_t physical = dma_base & 0x1FFFFFFFu", ADDITION)
        self.assertNotIn("{dma_base, uint32_t(c.data.size()), uint32_t(bytes)}", ADDITION)

        def snapshot_span(address, requested, stride):
            physical = address & ~(stride - 1)
            size = requested * stride
            if not size or size > (1 << 20) or physical + size > (1 << 29):
                return None
            return (physical, size)

        self.assertEqual(snapshot_span(0x1721D701, 24, 2),
                         (0x1721D700, 48))
        self.assertEqual(snapshot_span(0x1721D703, 24, 4),
                         (0x1721D700, 96))
        self.assertEqual(snapshot_span(0x1721D700, 24, 2),
                         (0x1721D700, 48))
        # Masking to 29 bits used to turn this INVALID host address into
        # a valid low-memory address and capture the wrong index bytes.
        self.assertIsNone(snapshot_span(0xA0010001, 24, 2))
        self.assertIsNone(snapshot_span(0x20000000, 24, 2))
        self.assertIsNone(snapshot_span(0x1FFFFFFF, 24, 4))
        self.assertIsNone(snapshot_span(0x1721D701, 0, 2))
        self.assertIsNone(snapshot_span(0x1721D700, 524289, 2))

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
            primitive = initiator & 0x3F
            if not (1 <= primitive <= 8 or 12 <= primitive <= 22):
                return False
            source = (initiator >> 6) & 3
            if source == 2:
                return True
            if source != 0:
                return False
            requested = initiator >> 16
            available = effective_size & 0xFFFFFF
            stride = 4 if (initiator >> 11) & 1 else 2
            physical = effective_base & ~(stride - 1)
            return (bool(requested) and requested <= available and
                    requested * stride <= (1 << 20) and
                    physical + requested * stride <= 0x20000000)
        self.assertTrue(accepted((24 << 16) | 4, 0x1721D700, 24))
        self.assertFalse(accepted((25 << 16) | 4, 0x1721D700, 24))
        self.assertFalse(accepted((24 << 16) | 4, 0x1721D700, 0))
        self.assertFalse(accepted((0 << 16) | 4, 0x1721D700, 24))
        self.assertFalse(accepted((24 << 16) | 4, 0x1FFFFFF0, 24))

if __name__=="__main__":unittest.main()
