#!/usr/bin/env python3
"""Regression: incomplete XDK PM4 may never reach Gather or CaptureDraw."""
import unittest
from patch_native_x_hulk_incomplete_xdk_draw import (patch, patch_enter, patch_header,
    ANCHOR, PATCH, ENTER_OLD, ENTER_NEW, PENDING_HEADER_OLD, PENDING_HEADER_NEW)

class IncompleteXdkTests(unittest.TestCase):
    def test_atomic_idempotent(self):
        source = "const TrackedCall* StateTracker::Leave(Call call, const uint8_t* base, bool nested) {\n" + ANCHOR
        out, changed = patch(source)
        self.assertTrue(changed)
        self.assertEqual(patch(out), (out, False))
        self.assertIn("if (!scan_.complete)", out)
        self.assertIn("if (!scan_.complete && call == Call::kPresent)", out)
        self.assertIn("[HULK/PRESENT-FRAME-RECOVERY]", out)
        self.assertIn("return &last_;", out)
        self.assertLess(out.index("if (!scan_.complete && call == Call::kPresent)"),
                        out.index("if (!scan_.complete)"))
        self.assertLess(out.index("scan_.Clear();\n    last_.call = call;"),
                        out.index("if (!scan_.complete)"))
        self.assertIn("last_.scan = &scan_;\n    return &last_;", out)
        self.assertIn("last_hulk_segment_valid_ = false;", out)
        self.assertIn("inline_begin_.pending = false;", out)
        self.assertIn("inline_begin_.end = 0;", out)
        self.assertLess(out.index("inline_begin_.pending = false;"), out.rindex("scan_.Clear();"))
        self.assertIn("scan_.Clear();\n    return nullptr;", out)
        self.assertLess(out.index("if (!scan_.complete)"), out.index(ANCHOR))
        self.assertIn("HULK/INCOMPLETE-XDK-DRAW-GUARD", out)
        self.assertIn("std::atomic<uint64_t>", out)
        self.assertIn("HULK/LOG192-PM4-BOUNDS", out)
        self.assertIn("end >= p.scan_from", out)
        self.assertIn("p.begin, p.scan_from, end, p.kick", out)
        self.assertIn("packet_draws={} indirect={}", out)
        # The original Present callback still runs; guard remains non-invasive.
        self.assertLess(out.index("if (!scan_.complete && call == Call::kPresent)"),
                        out.index("if (!scan_.complete)"))


    def test_only_revalidated_complete_draw_prefix_can_survive_bad_tail(self):
        # This must remain a NARROW recovery. Logs 200-202 mostly fail
        # before a GPU DRAW packet, which must continue to be discarded.
        cpp, changed = patch(
            "const TrackedCall* StateTracker::Leave(Call call, const uint8_t* base, bool nested) {\n"
            + ANCHOR)
        self.assertTrue(changed)
        start = cpp.index("HULK/XDK-VALIDATED-PREFIX-DRAW")
        finish = cpp.index("if (!scan_.complete && call == Call::kPresent)")
        guard = cpp[start:finish]
        self.assertIn("scan_.draws == 1", guard)
        self.assertIn("scan_.indirect == 0", guard)
        self.assertIn("scan_.hulk_fail_depth == 0", guard)
        self.assertIn("scan_.hulk_fail_packet == scan_.hulk_last_good_end", guard)
        self.assertIn("(scan_.hulk_last_good_header >> 30) == 3", guard)
        self.assertIn("hulk_last_opcode == 0x22u || hulk_last_opcode == 0x36u", guard)
        self.assertIn("ScanPackets(base, p.scan_from, scan_.hulk_last_good_end", guard)
        self.assertIn("hulk_validated_prefix.complete", guard)
        self.assertIn("hulk_validated_prefix.draws == 1", guard)
        self.assertIn("!hulk_validated_prefix.shader_loads.empty()", guard)
        for field in ("writes", "predicated_writes", "loads",
                      "shader_ucode", "register_order", "bin_mask", "bin_select"):
            self.assertIn("hulk_validated_prefix." + field + " == scan_." + field, guard)
        self.assertIn("hulk_same_shader_loads()", guard)
        self.assertIn("last_hulk_segment_valid_ = false;", guard)
        # The complete prefix scan has no failure-tail metadata after move.
        self.assertIn("const auto hulk_recovered_end = scan_.hulk_last_good_end;", guard)
        self.assertLess(guard.index("const auto hulk_recovered_end"), guard.index("scan_ = std::move(hulk_validated_prefix)"))
        self.assertIn("last_.end = GpuAddress(hulk_recovered_end);", guard)
        self.assertNotIn("last_.end = GpuAddress(scan_.hulk_last_good_end);", guard)
        self.assertIn("return &last_;", guard)
        self.assertNotIn("CaptureDraw(", guard)

        def valid(draws, indirect, depth, first, last, bad, header,
                  complete, reread_draws, has_shader, same):
            return (draws == 1 and indirect == 0 and depth == 0 and
                    first < last and last == bad and
                    header >> 30 == 3 and ((header >> 8) & 0x7f) in (0x22, 0x36)
                    and complete and reread_draws == 1 and has_shader and same)
        packet = 0xC0003600
        self.assertTrue(valid(1, 0, 0, 0x100, 0x120, 0x120, packet,
                              True, 1, True, True))
        self.assertFalse(valid(0, 0, 0, 0x100, 0x120, 0x120, packet,
                               True, 1, True, True))
        self.assertFalse(valid(1, 0, 0, 0x100, 0x120, 0x120, 0xC0025800,
                               True, 1, True, True))
        self.assertFalse(valid(1, 0, 0, 0x100, 0x120, 0x120, packet,
                               True, 1, True, False))
        self.assertFalse(valid(1, 0, 0, 0x100, 0x120, 0x120, packet,
                               False, 1, True, True))

    def test_rejected_draw_and_present_rollback_all_bin_state(self):
        cpp = ("void StateTracker::Enter(Call call, uint32_t device, const uint8_t* base, bool nested) {\n"
               + ENTER_OLD
               + "\n}\nconst TrackedCall* StateTracker::Leave(Call call, const uint8_t* base, bool nested) {\n"
               + ANCHOR)
        cpp, changed = patch_enter(cpp)
        self.assertTrue(changed)
        self.assertEqual(patch_enter(cpp), (cpp, False))
        cpp, changed = patch(cpp)
        self.assertTrue(changed)
        self.assertEqual(patch(cpp), (cpp, False))
        self.assertIn(ENTER_NEW, cpp)
        self.assertEqual(cpp.count("scan_.bin_mask = p.hulk_bin_mask_at_enter;"), 2)
        self.assertEqual(cpp.count("scan_.bin_select = p.hulk_bin_select_at_enter;"), 2)
        # Both incomplete branches must restore the outer Enter checkpoint
        # before Clear(), which intentionally retains BIN state.
        present = cpp.split("if (!scan_.complete && call == Call::kPresent)", 1)[1].split("if (!scan_.complete)", 1)[0]
        rejected = cpp.split("if (!scan_.complete) {", 1)[1].split("return nullptr;", 1)[0]
        # A failed Present still advances the frame; no stale inline draw
        # may survive that boundary or be submitted on a later call.
        for branch in (present, rejected):
            self.assertEqual(branch.count("inline_begin_.pending = false;"), 1)
            self.assertEqual(branch.count("inline_begin_.end = 0;"), 1)
            self.assertLess(branch.index("inline_begin_.end = 0;"), branch.index("scan_.Clear();"))
            for field in ("mask", "select"):
                restore = f"scan_.bin_{field} = p.hulk_bin_{field}_at_enter;"
                self.assertEqual(branch.count(restore), 1)
                self.assertLess(branch.index(restore), branch.index("scan_.Clear();"))
        self.assertLess(cpp.index("p.hulk_bin_mask_at_enter = scan_.bin_mask;"),
                        cpp.index("if (!scan_.complete && call == Call::kPresent)"))
        header, h_changed = patch_header(PENDING_HEADER_OLD)
        self.assertTrue(h_changed)
        self.assertEqual(patch_header(header), (header, False))
        self.assertIn(PENDING_HEADER_NEW, header)
        with self.assertRaises(ValueError):
            patch_enter(cpp.replace(ENTER_NEW, "  // missing XDK Enter checkpoint"))

    def test_source_drift_is_rejected(self):
        source = "const TrackedCall* StateTracker::Leave(Call call, const uint8_t* base, bool nested) {\n" + ANCHOR
        with self.assertRaises(ValueError):
            patch(source.replace(ANCHOR, "Gather(p, call, draw, base);"))
        with self.assertRaises(ValueError):
            patch(source + ANCHOR)
        with self.assertRaises(ValueError):
            patch("HULK/INCOMPLETE-XDK-DRAW-GUARD\n" + source)

if __name__ == "__main__":
    unittest.main()
