#!/usr/bin/env python3
"""Source-only regression tests for GPU log triage; never include game logs."""
import unittest
from hulk_render_log_audit import analyze, compare


class HulkLogAuditTests(unittest.TestCase):
    def test_message_counts_and_cumulative_replay_not_additive(self):
        log = "\n".join([
            "[error] 1D texture is too wide (5891328) - ignoring!",
            "[error] 1D texture has tiling enabled in the fetch constant",
            "[error] Primitive processor: Unsupported primitive type (48)",
            "[error] shader analysis failed: access violation",
            "[error] getCompTexLOD used with explicit LOD or gradients",
            "[HULK/PM4-SURFACE] skipped ambiguous synthetic RT/depth binding; total=10000",
            "[HULK/PM4-SURFACE] skipped ambiguous synthetic RT/depth binding; total=20000",
            "[HULK/PM4-MISS] slot=0 kind=color reason=identity_mismatch gpu_surface=0x12",
            "[HULK/PM4-DMA-REJECT] #1 missing DMA BASE/SIZE",
            "[HULK/EMPTY-DMA-INDEX] skipped indexed Draw",
            "[HULK/FETCH-WRITER] #1 call=0 draw=true slot=1 dword=3 value=0x44340000 indirect=0 source=SET",
            "[native_x_renderer] renderer: frame 600: 12 draw calls, 11 drawn, 1 without targets, 0 failed",
            "[native_x_renderer] renderer: frame 600: frame interval 20.18 ms average, 115.49 ms max",
            "[native_x_renderer] renderer: frame 600: draws skipped: 2 while a shader was translated, 3 while their pipeline compiled",
        ])
        r = analyze(log)
        self.assertEqual(r["cumulative_reported_totals"]["pm4_ambiguous_binding"], 20000)
        self.assertEqual(r["issue_messages_observed"]["invalid_texture_width"], 1)
        self.assertEqual(r["issue_messages_observed"]["pm4_missing_target"], 1)
        self.assertEqual(r["pm4_miss_samples"]["slot=0/color/identity_mismatch"], 1)
        self.assertEqual(r["fetch_writer_samples"]["slot=1/dword=3/source=SET/value=0x44340000"], 1)
        self.assertEqual(r["last_renderer_frame"]["draws"]["submitted"], 12)
        self.assertEqual(r["last_renderer_frame"]["draws"]["drawn"], 11)
        self.assertEqual(r["last_renderer_frame"]["interval_ms"]["average"], 20.18)
        self.assertEqual(r["last_renderer_frame"]["skipped_pipeline_compile"], 3)

    def test_separate_real_surface_misses_from_non_writable_draw_packets(self):
        text = "\n".join([
            "[HULK/PM4-NO-WRITABLE-RT] #1 surface=0xa020280 color_mask=0x0 depth_control=0x0",
            "[HULK/PM4-NO-WRITABLE-RT] #50000 surface=0xa020280 color_mask=0x0 depth_control=0x0",
            "[HULK/PM4-ACTUAL-MISS] #3 surface=0xa020280 color_mask=0xf depth_control=0x8777",
            "[HULK/PM4-ZERO-SURFACE] #12 surface=0x0",
            "[HULK/PM4-SURFACE] skipped ambiguous synthetic RT/depth binding; total=730000",
        ])
        audit = analyze(text)
        c = audit["cumulative_reported_totals"]
        self.assertEqual(c["pm4_no_writable_rt"], 50000)
        self.assertEqual(c["pm4_actual_surface_misses"], 3)
        self.assertEqual(c["pm4_zero_surface_pitch"], 12)
        self.assertEqual(c["pm4_ambiguous_binding"], 730000)
        self.assertEqual(audit["issue_messages_observed"]["pm4_no_writable_rt"], 2)
        self.assertEqual(audit["issue_messages_observed"]["pm4_actual_surface_miss"], 1)
        self.assertEqual(audit["issue_messages_observed"]["pm4_zero_surface_pitch"], 1)
        # Empty logs have unknown classifications, not proof of 0
        # actual GPU events. They report no observed counters.
        self.assertEqual(analyze("")["cumulative_reported_totals"]["pm4_no_writable_rt"], 0)
        self.assertIn("throttled", " ".join(audit["limitations"]))

    def test_later_frame_controls_values_and_empty_log(self):
        log = "\n".join([
            "[native_x_renderer] renderer: frame 100: 2 draw calls, 1 drawn, 0 without targets, 1 failed",
            "[native_x_renderer] renderer: frame 200: 4 draw calls, 4 drawn, 0 without targets, 0 failed",
            "[native_x_renderer] renderer: frame 100: frame interval 99.0 ms average, 111.0 ms max",
        ])
        r = analyze(log)
        self.assertEqual(r["last_renderer_frame"]["frame"], 200)
        self.assertNotIn("interval_ms", r["last_renderer_frame"])
        self.assertEqual(analyze("")["last_renderer_frame"], {})

    def test_candidate_comparison_does_not_claim_quality(self):
        b = analyze("[error] 1D texture is too wide (5891328)\n")
        c = analyze("")
        delta = compare(b, c)
        self.assertEqual(delta["issue_deltas"]["invalid_texture_width"]["delta"], -1)
        self.assertIn("not proof", delta["comparison_warning"])


if __name__ == "__main__":
    unittest.main()
