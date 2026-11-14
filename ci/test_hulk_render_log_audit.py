#!/usr/bin/env python3
"""Source-only regression tests for GPU log triage; never include game logs."""
import unittest
from hulk_render_log_audit import analyze, compare, shader_static_cf


class HulkLogAuditTests(unittest.TestCase):
    def test_raw_constants_and_fetch_divergence_survive_zero_ps_mismatches(self):
        log = "\n".join([
            "verify: frames 1800..1800: CPU draw calls 4",
            "4 of 4 draw calls have no mismatches in compared state; executions missing VS or PS analysis: 4",
            "verify: texture fetch slot differences (all slots, read or not):",
            "    tfetch3 x2 (example omitted)",
            "    tfetch4 x2 (example omitted)",
            "verify: VS float constant mismatches (1024 compared):",
            "    c21 x3 (example omitted)",
            "    c22 x3 (example omitted)",
            "    ... 108 more",
            "verify: PS float constant mismatches (1024 compared): none",
            "verify: pixel program mismatches (tracker vs CP): 0 draws",
            "unrelated: c99 x88",
            "verify: frames 1801..1801: CPU draw calls 1",
            "verify: VS float constant mismatches (0 compared): none",
        ])
        samples = analyze(log)["gpu_verification_samples"]
        a, b = samples
        self.assertEqual(a["pixel_program_mismatches"], 0)
        self.assertEqual(a["missing_shader_analysis_executions"], 4)
        self.assertEqual(a["texture_fetch_differences"]["entries"], {"3":2,"4":2})
        self.assertEqual(a["float_constants"]["VS"]["entries"], {"21":3,"22":3})
        self.assertEqual(a["float_constants"]["VS"]["omitted_entries"], 108)
        self.assertFalse(a["float_constants"]["VS"]["reported_none"])
        self.assertTrue(a["float_constants"]["PS"]["reported_none"])
        self.assertEqual(b["float_constants"]["VS"]["compared_vec4"], 0)
        self.assertIsNone(b["missing_shader_analysis_executions"])
        self.assertIsNone(b["texture_fetch_differences"])
        self.assertNotIn("PS", b["float_constants"])

    def test_xenos_vs_family_static_exec_and_safe_log_203_classification(self):
        # Synthetic 27-word CF: two EXEC packets (instructions 3-4 and 5),
        # two ALLOC, and NOPs. The deliberately altered trailing word is
        # NOT borrowed game microcode and is not in a direct EXEC range.
        base = [0] * 27
        base[:6] = [0x00002003, 0x00001000, 0xC0000000,
                    0x00001005, 0x00001000, 0xC0000000]
        used, execs, cf_pairs = shader_static_cf(base)
        self.assertEqual(cf_pairs, 3)
        self.assertEqual(execs, ((3, 2), (5, 1)))
        self.assertEqual(len(used), 18)
        self.assertNotIn(23, used)
        a, b = base[:], base[:]
        a[23], b[23] = 0x01234567, 0x89ABCDEF
        def failed(shader_hash, payload):
            return ("[native_x_renderer] vertex shader " + shader_hash +
                    " (27 dwords): analysis failed: access violation\\n" +
                    "[native_x_renderer] the vertex program came from " +
                    "IM_LOAD_IMMEDIATE at 0xa2001000 (packet 0xa2000ff4) " +
                    "in DrawIndexedVertices: " +
                    " ".join(f"{w:08X}" for w in payload))
        log = failed("1234567890ABCDEF", a) + "\\n" + failed("FEDCBA0987654321", b)
        summary = analyze(log)
        family = summary["shader_analysis_families"]
        self.assertEqual(len(family), 1)
        self.assertEqual(family[0]["failure_count"], 2)
        self.assertEqual(family[0]["varying_word_indices"], [23])
        self.assertTrue(family[0]["varying_words_outside_direct_exec"])
        self.assertEqual(family[0]["direct_exec_ranges"], [[3, 2], [5, 1]])
        self.assertEqual(family[0]["load_kinds"], ["IM_LOAD_IMMEDIATE"])
        # Public JSON must never contain shader instruction payloads.
        self.assertNotIn("01234567", str(family))
        self.assertNotIn("89ABCDEF", str(family))
        self.assertEqual(summary["shader_failures_without_static_cf"], 0)
        self.assertIsNone(shader_static_cf([0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF]))
        self.assertIsNone(shader_static_cf(base[:17]))

    def test_log203_resolve_gpu_and_upload_cost_fields(self):
        log = "\\n".join([
            "[native_x_renderer] renderer: frame 15600: 9 clears, 28 resolves (0 skipped)",
            "[native_x_renderer] renderer: frame 15600: 1887 draw calls, 1887 drawn, "
            "0 without targets, 0 failed; GPU 22.36 ms average, 76.72 ms max",
            "[native_x_renderer] renderer: frame 15600: per frame, "
            "4524.5 buffer copies average, 7187 max; guest memory 12484 KB copied",
            "[native_x_renderer] renderer: frame 15600: frame interval 23.25 ms average, "
            "122.82 ms max; renderer CPU 11.54 ms per frame; "
            "waiting for the GPU 10.33 ms per frame",
        ])
        frame = analyze(log)["last_renderer_frame"]
        self.assertEqual(frame["frame"], 15600)
        self.assertEqual(frame["resolve_stats"]["resolves"], 28)
        self.assertEqual(frame["gpu_ms"]["average"], 22.36)
        self.assertEqual(frame["buffer_copies_per_frame"]["average"], 4524.5)
        self.assertEqual(frame["buffer_copies_per_frame"]["guest_kb_copied"], 12484)
        self.assertEqual(frame["render_cpu_ms"], 11.54)
        self.assertEqual(frame["gpu_wait_ms"], 10.33)
        self.assertEqual(frame["interval_ms"]["max"], 122.82)

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

    def test_log200_event_write_tail_classifies_without_guessing_draw_boundary(self):
        log = "\n".join([
            "[HULK/INCOMPLETE-XDK-DRAW] #1 call=0 range_ok=true "
            "packet_draws=0 fail_site=152 fail_type=1 fail_opcode=0x0 "
            "fail_depth=0 fail_packet=0xa28c5114 fail_header=0x44200000 "
            "last_good_end=0xa28c5110 last_good_header=0xc0025800",
            "[HULK/INCOMPLETE-XDK-DRAW] #2 call=0 range_ok=true "
            "packet_draws=0 fail_site=142 fail_type=0 fail_opcode=0x0 "
            "fail_depth=0 fail_packet=0xa296e500 fail_header=0x3f800000 "
            "last_good_end=0xa296e4dc last_good_header=0xc0025800",
            "[HULK/INCOMPLETE-XDK-DRAW] #3 call=0 range_ok=true "
            "packet_draws=24 fail_site=160 fail_type=2 fail_opcode=0x0 "
            "fail_depth=0 fail_packet=0xa2788ddc fail_header=0xb1800001 "
            "last_good_end=0xa2788dd8 last_good_header=0xb2200",
            "[HULK/INCOMPLETE-XDK-DRAW] #10000 call=0 range_ok=false "
            "packet_draws=0 fail_site=0 fail_type=0 fail_opcode=0x0 "
            "fail_depth=0 fail_packet=0x0 last_good_end=0x0 last_good_header=0x0",
        ])
        report = analyze(log)
        observed = report["incomplete_xdk_tail_context_samples"]
        self.assertEqual(observed["last_event_write_shd"], 2)
        self.assertEqual(observed["other_last_good_packet"], 1)
        self.assertEqual(observed["missing_valid_last_good_packet"], 1)
        self.assertNotIn("fail_distance_from_last_good_end_0_bytes", observed)
        self.assertEqual(observed["samples_with_draw_prefix"], 1)
        self.assertEqual(observed["draws_in_rejected_sample_prefixes"], 24)
        self.assertEqual(observed["fail_distance_from_last_good_end_4_bytes"], 2)
        self.assertEqual(observed["fail_distance_from_last_good_end_36_bytes"], 1)
        self.assertIn("not a verified end-of-call marker",
                      " ".join(report["limitations"]))
        self.assertEqual(report["incomplete_xdk_draw_samples"]["throttled_warnings"], 4)

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
            "missing_shader_analysis_executions": None,
            "float_constants": {}, "texture_fetch_differences": None,
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
