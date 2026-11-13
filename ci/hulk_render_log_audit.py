#!/usr/bin/env python3
"""Local-only, source-safe Hulk NativeX log classifier and build comparison.

No game assets or log excerpts are copied into reports. Input logs remain local.
Counts of throttled/sampled warnings are *observed messages*, not total events.
"""
from __future__ import annotations
import argparse
from collections import Counter
import json
from pathlib import Path
import re

ISSUES = {
    "invalid_texture_width": re.compile(r"\b1D texture is too wide\b"),
    "invalid_texture_tiling": re.compile(r"\b1D texture has tiling enabled\b"),
    "unsupported_primitive": re.compile(r"\bUnsupported primitive type\b", re.I),
    "shader_analysis_access_violation": re.compile(r"\banalysis failed: access violation\b", re.I),
    "shader_analysis_null_pointer": re.compile(r"\banalysis failed: string pointer is null\b", re.I),
    "invalid_get_computed_lod": re.compile(r"getCompTexLOD used with explicit LOD or gradients"),
    "pm4_missing_target": re.compile(r"\[HULK/PM4-MISS\]"),
    "pm4_dma_rejected": re.compile(r"\[HULK/PM4-DMA-REJECT\]"),
    # Source-only split of ambiguous surface misses, available after #89fbb93:
    "pm4_no_writable_rt": re.compile(r"\[HULK/PM4-NO-WRITABLE-RT\]"),
    "pm4_actual_surface_miss": re.compile(r"\[HULK/PM4-ACTUAL-MISS\]"),
    "pm4_zero_surface_pitch": re.compile(r"\[HULK/PM4-ZERO-SURFACE\]"),
    "empty_dma_draw": re.compile(r"\[HULK/EMPTY-DMA-INDEX\]"),
    "fetch_writer": re.compile(r"\[HULK/FETCH-WRITER\]"),
    "incomplete_xdk_draw": re.compile(r"\[HULK/INCOMPLETE-XDK-DRAW\]"),
}
CUMULATIVE = {
    "pm4_ambiguous_binding": re.compile(r"\[HULK/PM4-SURFACE\] skipped ambiguous synthetic RT/depth binding; total=(\d+)"),
    "pm4_packet_draws_accepted": re.compile(r"\[HULK/PM4-ACCEPT\].*?total=(\d+)"),
    # These three counts are throttled. Retain the highest #N and NEVER
    # sum their log lines or present a missing sample as zero GPU work.
    "pm4_no_writable_rt": re.compile(r"\[HULK/PM4-NO-WRITABLE-RT\] #(\d+)"),
    "pm4_actual_surface_misses": re.compile(r"\[HULK/PM4-ACTUAL-MISS\] #(\d+)"),
    "pm4_zero_surface_pitch": re.compile(r"\[HULK/PM4-ZERO-SURFACE\] #(\d+)"),
    "incomplete_xdk_draws": re.compile(r"\[HULK/INCOMPLETE-XDK-DRAW\] #(\d+)"),
}
RT_MISS = re.compile(r"\[HULK/PM4-MISS\] slot=(\d+) kind=(\w+) reason=([\w_]+)")
FETCH_WRITER = re.compile(r"\[HULK/FETCH-WRITER\].*?slot=(\d+) dword=(\d+) value=(0x[0-9a-fA-F]+).*?source=(\w+)")
# HULK/LOG194-GAP-TRIAGE: the #N warning is throttled (first 16, then
# every 10,000). Keep its cumulative maximum and distinguish the few
# diagnostic samples from the actual number of discarded calls.
INCOMPLETE_XDK = re.compile(r"\[HULK/INCOMPLETE-XDK-DRAW\] #(\d+)")
INCOMPLETE_META = re.compile(r"\brange_ok=(true|false)\b.*?\bpacket_draws=(\d+)")
INCOMPLETE_FAIL = re.compile(
    r"\bfail_site=(\d+)\b.*?\bfail_type=(\d+)\b.*?"
    r"\bfail_opcode=(0x[0-9a-fA-F]+)\b.*?\bfail_depth=(\d+)"
)

# #198: "last good" headers can themselves be impossible Type1/Type2
# values (for example 0x44800000 = IEEE754 1024.0), so the old scanner
# silently consumed data as two GPU register writes. Do not treat a
# successfully advanced pointer as proof of a real PM4 packet.
LAST_GOOD_HEADER = re.compile(r"\blast_good_header=(0x[0-9a-fA-F]+)")
VERIFY_FRAME = re.compile(r"verify: frames (\d+)\.\.(\d+): CPU draw calls (\d+)")
VERIFY_MATCH = re.compile(
    r"(\d+) of (\d+) draw calls have no mismatches in compared state"
)
VERIFY_PS = re.compile(
    r"verify: pixel program mismatches .*?: (\d+) draws"
)

VERIFY_FLOAT = re.compile(r"verify: (VS|PS) float constant mismatches \((\d+) compared\):\s*(none)?")
VERIFY_COVERAGE = re.compile(r"executions missing VS or PS analysis: (\d+)")
FLOAT_ENTRY = re.compile(r"^\s+c(\d+) x(\d+)\b")
FETCH_ENTRY = re.compile(r"^\s+tfetch(\d+) x(\d+)\b")
OMITTED_ENTRIES = re.compile(r"^\s+\.\.\. (\d+) more")


def reserved_pm4_header(word: int) -> str | None:
    kind = word >> 30
    if kind == 1 and word & 0x3FC00000:
        return "type1_reserved_bits"
    if kind == 2 and word != 0x80000000:
        return "type2_reserved_bits"
    return None


FRAME_STATS = re.compile(r"\[native_x_renderer\] renderer: frame (\d+):\s*(.*)")
DRAWS = re.compile(r"(\d+) draw calls, (\d+) drawn, (\d+) without targets, (\d+) failed")
FPS = re.compile(r"frame interval\s+([\d.]+) ms average,\s+([\d.]+) ms max")
PIPELINES = re.compile(r"draws skipped: (\d+) while a shader was translated, (\d+) while their pipeline compiled")
# A scene that is natively rasterized at 2560x1440 can be immediately
# resolved into 1280x720. Report this separately from the output swapchain.
# It may be intentional post-processing; not proof of final-frame blur.
SCENE_RESOLVE = re.compile(
    r"\[HULK/SCENE-RESOLVE\] #\d+ src guest=(\d+)x(\d+) (\d+)xMSAA "
    r"src host=(\d+)x(\d+) dst guest=(\d+)x(\d+) dst host=(\d+)x(\d+)"
)


def analyze(text: str) -> dict:
    counts: Counter[str] = Counter()
    rt_misses: Counter[str] = Counter()
    writers: Counter[str] = Counter()
    xdk_draw_samples: Counter[str] = Counter()
    xdk_failure_sites: Counter[str] = Counter()
    malformed_previous_headers: Counter[str] = Counter()
    verifier_samples: list[dict[str, object]] = []
    cumulative: dict[str, int] = {name: 0 for name in CUMULATIVE}
    latest_frame: dict[str, object] = {}
    frames_seen: set[int] = set()
    scene_resolves: Counter[tuple[int, ...]] = Counter()
    detail_block = None
    for line in text.splitlines():
        if line.lstrip().startswith("[") or "verify:" in line:
            detail_block = None
        scene = SCENE_RESOLVE.search(line)
        if scene:
            scene_resolves[tuple(int(scene[k]) for k in range(1, 10))] += 1
        for name, pattern in ISSUES.items():
            if pattern.search(line):
                counts[name] += 1
        incomplete = INCOMPLETE_XDK.search(line)
        if incomplete:
            xdk_draw_samples["throttled_warnings"] += 1
            metadata = INCOMPLETE_META.search(line)
            if metadata:
                xdk_draw_samples["valid_range" if metadata[1] == "true"
                                 else "invalid_range"] += 1
                if int(metadata[2]) == 0:
                    xdk_draw_samples["zero_packet_draws"] += 1
            else:
                xdk_draw_samples["missing_range_metadata"] += 1
            previous_header = LAST_GOOD_HEADER.search(line)
            if previous_header:
                cause = reserved_pm4_header(int(previous_header[1], 16))
                if cause:
                    malformed_previous_headers[cause] += 1
            failure = INCOMPLETE_FAIL.search(line)
            if failure:
                key = (f"site={int(failure[1])}/type={int(failure[2])}/"
                       f"opcode={failure[3].lower()}/depth={int(failure[4])}")
                xdk_failure_sites[key] += 1
            else:
                xdk_draw_samples["missing_first_failure_metadata"] += 1
        match = RT_MISS.search(line)
        if match:
            rt_misses["slot=" + match[1] + "/" + match[2] + "/" + match[3]] += 1
        match = FETCH_WRITER.search(line)
        if match:
            writers[f"slot={match[1]}/dword={match[2]}/source={match[4]}/value={match[3].lower()}"] += 1
        for name, pattern in CUMULATIVE.items():
            match = pattern.search(line)
            if match:
                cumulative[name] = max(cumulative[name], int(match[1]))
        frame_verify = VERIFY_FRAME.search(line)
        if frame_verify:
            verifier_samples.append({
                "first_frame": int(frame_verify[1]),
                "last_frame": int(frame_verify[2]),
                "cpu_draw_calls": int(frame_verify[3]),
                "matched_state": None,
                "matched_total": None,
                "pixel_program_mismatches": None,
                "missing_shader_analysis_executions": None,
                "float_constants": {},
                "texture_fetch_differences": None,
            })
        if verifier_samples:
            verified = VERIFY_MATCH.search(line)
            if verified:
                verifier_samples[-1]["matched_state"] = int(verified[1])
                verifier_samples[-1]["matched_total"] = int(verified[2])
            coverage = VERIFY_COVERAGE.search(line)
            if coverage:
                verifier_samples[-1]["missing_shader_analysis_executions"] = int(coverage[1])
            floats = VERIFY_FLOAT.search(line)
            if floats:
                detail_block = {"compared_vec4": int(floats[2]),
                                "reported_none": bool(floats[3]),
                                "entries": {}, "omitted_entries": 0}
                verifier_samples[-1]["float_constants"][floats[1]] = detail_block
            elif "verify: texture fetch slot differences" in line:
                detail_block = {"reported_none": line.rstrip().endswith(": none"),
                                "entries": {}, "omitted_entries": 0}
                verifier_samples[-1]["texture_fetch_differences"] = detail_block
            elif detail_block is not None:
                entry = (FLOAT_ENTRY if "compared_vec4" in detail_block else FETCH_ENTRY).search(line)
                omitted = OMITTED_ENTRIES.search(line)
                if entry:
                    detail_block["entries"][entry[1]] = int(entry[2])
                elif omitted:
                    detail_block["omitted_entries"] = int(omitted[1])
                elif line.strip():
                    detail_block = None
            program = VERIFY_PS.search(line)
            if program:
                verifier_samples[-1]["pixel_program_mismatches"] = int(program[1])
        match = FRAME_STATS.search(line)
        if not match:
            continue
        frame = int(match[1])
        frames_seen.add(frame)
        if frame < int(latest_frame.get("frame", -1)):
            continue
        latest_frame["frame"] = frame
        rest = match[2]
        draws = DRAWS.search(rest)
        if draws:
            latest_frame["draws"] = {
                "submitted": int(draws[1]), "drawn": int(draws[2]),
                "no_targets": int(draws[3]), "failed": int(draws[4]),
            }
        interval = FPS.search(rest)
        if interval:
            latest_frame["interval_ms"] = {"average": float(interval[1]), "max": float(interval[2])}
        pipelines = PIPELINES.search(rest)
        if pipelines:
            latest_frame["skipped_shader_translation"] = int(pipelines[1])
            latest_frame["skipped_pipeline_compile"] = int(pipelines[2])
    return {
        "issue_messages_observed": {key: counts[key] for key in ISSUES},
        "pm4_miss_samples": dict(sorted(rt_misses.items())),
        "fetch_writer_samples": dict(sorted(writers.items())),
        "incomplete_xdk_draw_samples": dict(sorted(xdk_draw_samples.items())),
        "pm4_first_failure_sample_sites": dict(sorted(xdk_failure_sites.items())),
        "pm4_impossible_last_good_header_samples": dict(
            sorted(malformed_previous_headers.items())),
        "gpu_verification_samples": verifier_samples,
        "cumulative_reported_totals": cumulative,
        "last_renderer_frame": latest_frame,
        "renderer_stat_frames_observed": sorted(frames_seen),
        "scene_resolve_host_resolution": [
            {
                "src_guest": [g_w, g_h], "msaa_samples": samples,
                "src_host": [src_w, src_h],
                "dst_guest": [d_w, d_h], "dst_host": [dst_w, dst_h],
                "logged_samples": n,
                "source_to_destination_area_ratio": round(
                    src_w * src_h / (dst_w * dst_h), 4) if dst_w and dst_h else None,
                "intermediate_resolution_loss": src_w > dst_w or src_h > dst_h,
                "note": "Observed intermediate Resolve dimensions; final-frame quality not established",
            }
            for (g_w, g_h, samples, src_w, src_h, d_w, d_h, dst_w, dst_h), n
            in sorted(scene_resolves.items())
        ],
        "limitations": [
            "A smaller intermediate Resolve may be intentional: confirm subsequent shader sampling and final image before claiming pixelation fixed.",
            "Some warnings are throttled; counts are logged samples, not total GPU errors.",
            "HULK incomplete XDK #N is cumulative, while the range/failure-site breakdown covers sampled log warnings ONLY.",
            "PM4-SURFACE total is a cumulative replay counter, not a count of distinct discarded draw objects.",
            "New PM4 classifications are last observed throttled #N counters, not complete counts if samples are missing.",
            "No-writable attachment is not necessarily a failed scene geometry draw (queries/pipe helpers may issue PM4 draws).",
            "Same-frame scene, game area, camera and settings are needed to infer improvement between logs.",
            "Float/fetch entry counts overlap across draws; never sum them into a count of bad draws. Omitted entries are not reconstructed.",
            "Missing usage analysis means raw float windows were compared; mismatching unused registers alone do not prove a rendering defect.",
            "GPU verifier compares selected state only; a 100% match does not prove shader semantics or absence of flicker.",
            "Malformed last-good header counts are sampled warning evidence, not complete rejected-packet totals.",
            "CI build success and log improvements cannot establish pixel-perfect GPU output.",
        ],
    }


def compare(baseline: dict, candidate: dict) -> dict:
    changes = {}
    for name in ISSUES:
        before = baseline["issue_messages_observed"].get(name, 0)
        after = candidate["issue_messages_observed"].get(name, 0)
        changes[name] = {"baseline": before, "candidate": after, "delta": after - before}
    for name in CUMULATIVE:
        before = baseline["cumulative_reported_totals"].get(name, 0)
        after = candidate["cumulative_reported_totals"].get(name, 0)
        changes[name] = {"baseline": before, "candidate": after, "delta": after - before}
    return {
        "issue_deltas": changes,
        "comparison_warning": "Deltas are only meaningful for comparable gameplay and logging settings, not proof of visual quality.",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Local Hulk runtime log, never committed")
    parser.add_argument("--compare", type=Path, help="Local candidate log")
    parser.add_argument("--output", type=Path, help="Optional local JSON report output")
    a = parser.parse_args()
    baseline = analyze(a.log.read_text(encoding="utf-8", errors="replace"))
    report = {"baseline": baseline}
    if a.compare:
        candidate = analyze(a.compare.read_text(encoding="utf-8", errors="replace"))
        report.update(candidate=candidate, comparison=compare(baseline, candidate))
    txt = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if a.output:
        a.output.write_text(txt, encoding="utf-8")
    print(txt)


if __name__ == "__main__":
    main()
