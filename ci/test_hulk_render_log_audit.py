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

    def test_log194_incomplete_draw_count_and_first_failure_samples(self):
        # 130,000 rejected calls are represented by THREE throttled lines here,
        # not 130,000 detailed samples. Never sum the warning IDs.
        log = "\n".join([
            "[HULK/INCOMPLETE-XDK-DRAW] #1 call=0 was_draw=true "
            "begin=0xa2728794 scan_from=0xa2728794 end=0xa2728d04 "
            "segment_kick=false range_ok=true bytes=1392 packet_draws=0 "
            "indirect=0 writes=4 loads=0 shader_loads=1 "
            "fail_site=412 fail_packet=0xa2728d00 fail_header=0xc0002d00 "
            "fail_type=3 fail_opcode=0x2d fail_depth=1 fail_remaining=0 - discarded",
            "[HULK/PRESENT-FRAME-RECOVERY] #99999 incomplete PM4 tail; "
            "preserved original XDK Present",
            "[HULK/INCOMPLETE-XDK-DRAW] #130000 call=0 was_draw=true "
            "begin=0xa296bbe4 scan_from=0xa296bbe4 end=0xa296c7bc "
            "segment_kick=false range_ok=true bytes=3032 packet_draws=0 "
            "indirect=0 writes=0 loads=0 shader_loads=0 "
            "fail_site=412 fail_packet=0xa296c7b8 fail_header=0xc0002d00 "
            "fail_type=3 fail_opcode=0x2d fail_depth=1 fail_remaining=0 - discarded",
            "[HULK/INCOMPLETE-XDK-DRAW] #129999 call=0 was_draw=true "
            "range_ok=false bytes=0 packet_draws=0 indirect=0 writes=0 "
            "loads=0 shader_loads=0 - discarded",
        ])
        result = analyze(log)
        self.assertEqual(result["cumulative_reported_totals"]
                         ["incomplete_xdk_draws"], 130000)
        samples = result["incomplete_xdk_draw_samples"]
        self.assertEqual(samples["throttled_warnings"], 3)
        self.assertEqual(samples["valid_range"], 2)
        self.assertEqual(samples["invalid_range"], 1)
        self.assertEqual(samples["zero_packet_draws"], 3)
        self.assertEqual(samples["missing_first_failure_metadata"], 1)
        self.assertEqual(result["pm4_first_failure_sample_sites"]
                         ["site=412/type=3/opcode=0x2d/depth=1"], 2)
        self.assertEqual(result["issue_messages_observed"]
                         ["incomplete_xdk_draw"], 3)
        self.assertEqual(analyze("")["cumulative_reported_totals"]
                         ["incomplete_xdk_draws"], 0)
        self.assertIn("sampled", " ".join(result["limitations"]))

    def test_log198_false_last_good_and_program_mismatch(self):
        sample = "\n".join([
            "[HULK/INCOMPLETE-XDK-DRAW] #1 range_ok=true bytes=984 "
            "packet_draws=0 fail_site=138 fail_type=0 fail_opcode=0x0 "
            "fail_depth=0 last_good_header=0x44800000 - discarded",
            "[HULK/INCOMPLETE-XDK-DRAW] #2 range_ok=true bytes=984 "
            "packet_draws=0 fail_site=138 fail_type=0 fail_opcode=0x0 "
            "fail_depth=0 last_good_header=0x44340000 - discarded",
            "[HULK/INCOMPLETE-XDK-DRAW] #3 range_ok=true bytes=984 "
            "packet_draws=0 fail_site=138 fail_type=0 fail_opcode=0x0 "
            "fail_depth=0 last_good_header=0xb1800001 - discarded",
            "[HULK/INCOMPLETE-XDK-DRAW] #4 range_ok=true bytes=984 "
            "packet_draws=0 fail_site=138 fail_type=0 fail_opcode=0x0 "
            "fail_depth=0 last_good_header=0xc0025800 - discarded",
            "[native_x_renderer] verify: frames 2324..2324: CPU draw calls 1569: "
            "DrawIndexedVertices=1546 DrawVertices=23",
            "[native_x_renderer] verify: GPU draw packets of the verified frames "
            "1620: 1569 matched to a D3D draw call; 1518 of 1569 draw calls "
            "have no mismatches in compared state (not full shader validation)",
            "[native_x_renderer] verify: pixel program mismatches "
            "(tracker's microcode vs the command processor's): "
            "50 draws, 0 with an inline program",
        ])
        report = analyze(sample)
        self.assertEqual(report["pm4_impossible_last_good_header_samples"],
                         {"type1_reserved_bits": 2, "type2_reserved_bits": 1})
        self.assertEqual(report["gpu_verification_samples"], [{
            "first_frame": 2324, "last_frame": 2324,
            "cpu_draw_calls": 1569, "matched_state": 1518,
            "matched_total": 1569, "pixel_program_mismatches": 50,
        }])

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

    def test_native_scene_resolve_reports_real_intermediate_pixel_loss(self):
        log = "\n".join([
            "[HULK/SCENE-RESOLVE] #1 src guest=640x360 4xMSAA src host=2560x1440 "
            "dst guest=640x360 dst host=1280x720 flags=0x0",
            "[HULK/SCENE-RESOLVE] #2 src guest=640x360 4xMSAA src host=2560x1440 "
            "dst guest=640x360 dst host=1280x720 flags=0x0",
            "[HULK/SCENE-RESOLVE] #3 src guest=640x360 4xMSAA src host=2560x1440 "
            "dst guest=640x360 dst host=2560x1440 flags=0x0",
        ])
        r = analyze(log)["scene_resolve_host_resolution"]
        self.assertEqual(len(r), 2)
        smaller = next(x for x in r if x["intermediate_resolution_loss"])
        self.assertEqual(smaller["logged_samples"], 2)
        self.assertEqual(smaller["source_to_destination_area_ratio"], 4.0)
        self.assertEqual(smaller["dst_host"], [1280, 720])
        self.assertFalse(next(x for x in r if not x["intermediate_resolution_loss"])["intermediate_resolution_loss"])
        self.assertEqual(analyze("")["scene_resolve_host_resolution"], [])
        self.assertIn("final image", " ".join(analyze(log)["limitations"]))

    def test_candidate_comparison_does_not_claim_quality(self):
        b = analyze("[error] 1D texture is too wide (5891328)\n")
        c = analyze("")
        delta = compare(b, c)
        self.assertEqual(delta["issue_deltas"]["invalid_texture_width"]["delta"], -1)
        self.assertIn("not proof", delta["comparison_warning"])


if __name__ == "__main__":
    unittest.main()
