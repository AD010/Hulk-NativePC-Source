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
FETCH_REJECT_OWNER = re.compile(
    r"\[HULK/INVALID-(VERTEX|TEXTURE)-DRAW\].*?slot=(\d+).*?"
    r"VS=([0-9a-fA-F]{16})(?:\s+PS=([0-9a-fA-F]{16}))?"
)
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
LAST_GOOD_END = re.compile(r"\blast_good_end=(0x[0-9a-fA-F]+)")
FAIL_PACKET = re.compile(r"\bfail_packet=(0x[0-9a-fA-F]+)")
# 0xC0025800 is exactly Type3 EVENT_WRITE_SHD with a three-word payload.
# It may be followed by legitimate PM4; it is NOT a safe resync delimiter.
EVENT_WRITE_SHD_HEADER = 0xC0025800
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


# HULK/VS-FAMILY-203: correlate a failed vertex analysis with the next
# authentic IM_LOAD source log. Raw game microcode is never returned or
# written to JSON; only opcode-based structure and hash identifiers remain.
VERTEX_FAILURE = re.compile(
    r"vertex shader ([0-9A-Fa-f]{16}) \((\d+) dwords\): analysis failed: (.+)$")
VERTEX_IMMEDIATE = re.compile(
    r"the vertex program came from (IM_LOAD(?:_IMMEDIATE)?) at "
    r"(0x[0-9A-Fa-f]+) \(packet (0x[0-9A-Fa-f]+)\) in "
    r"(\w+): ((?:[0-9A-Fa-f]{8}(?:\s+|$))+)")
EXEC_CF_OPS = {1, 2, 3, 4, 5, 6}
# Loops, calls, jumps and unrecognized CF must be inspected manually.
STATIC_CF_OPS = {0, 12}

def shader_static_cf(words: list[int]) -> tuple[tuple[int, ...], tuple[tuple[int, int], ...], int] | None:
    """Trusted direct EXEC word indices only; return None for dynamic CF."""
    if not words or len(words) % 3 or len(words) > 65536:
        return None
    pairs = len(words) // 3
    bound = pairs
    def decode(i: int) -> tuple[tuple[int, int], tuple[int, int]]:
        a, b, d = words[3*i:3*i+3]
        return ((a, b & 0xFFFF),
                (((b >> 16) | ((d & 0xFFFF) << 16)), d >> 16))
    i = 0
    while i < bound:
        for word0, word1 in decode(i):
            opcode = (word1 >> 12) & 15
            if opcode in EXEC_CF_OPS:
                bound = min(bound, word0 & 0xFFF)
            elif opcode not in STATIC_CF_OPS:
                return None
        i += 1
    if bound <= 0:
        return None
    used = set(range(bound * 3))
    execs = []
    for i in range(bound):
        for word0, word1 in decode(i):
            if ((word1 >> 12) & 15) in EXEC_CF_OPS:
                start, count = word0 & 0xFFF, (word0 >> 12) & 7
                if start > pairs or count > pairs - start:
                    return None
                used.update(range(start * 3, (start + count) * 3))
                execs.append((start, count))
    return tuple(sorted(used)), tuple(execs), bound

RESOLVE_STATS = re.compile(r"(\d+) clears, (\d+) resolves \((\d+) skipped\)")
GPU_COST = re.compile(r"GPU ([\d.]+) ms average, ([\d.]+) ms max")
BUFFER_COST = re.compile(r"per frame, ([\d.]+) buffer copies average, (\d+) max; guest memory (\d+) KB copied")
CPU_COST = re.compile(r"renderer CPU ([\d.]+) ms per frame; waiting for the GPU ([\d.]+) ms per frame")

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
    invalid_fetch_shader_owners: Counter[str] = Counter()
    xdk_draw_samples: Counter[str] = Counter()
    xdk_failure_sites: Counter[str] = Counter()
    malformed_previous_headers: Counter[str] = Counter()
    xdk_tail_context: Counter[str] = Counter()
    verifier_samples: list[dict[str, object]] = []
    cumulative: dict[str, int] = {name: 0 for name in CUMULATIVE}
    latest_frame: dict[str, object] = {}
    frames_seen: set[int] = set()
    scene_resolves: Counter[tuple[int, ...]] = Counter()
    detail_block = None
    shader_failure_pending = None
    shader_family_inputs = []
    for line in text.splitlines():
        failure = VERTEX_FAILURE.search(line)
        if failure:
            shader_failure_pending = (failure[1].lower(), int(failure[2]), failure[3])
        elif shader_failure_pending is not None:
            origin = VERTEX_IMMEDIATE.search(line)
            if origin:
                words = [int(w, 16) for w in origin[5].split()]
                if len(words) == shader_failure_pending[1]:
                    shader_family_inputs.append((
                        shader_failure_pending[0], words, origin[1],
                        origin[2], origin[3], origin[4]))
            shader_failure_pending = None
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
            if not previous_header or not metadata or metadata[1] != "true" or (
                    int(previous_header[1], 16) == 0):
                xdk_tail_context["missing_valid_last_good_packet"] += 1
            else:
                word = int(previous_header[1], 16)
                cause = reserved_pm4_header(word)
                if cause:
                    malformed_previous_headers[cause] += 1
                if word == EVENT_WRITE_SHD_HEADER:
                    # HULK/XDK-TOPLEVEL-TAIL: 13 sampled examples in #200
                    # terminate soon after EVENT_WRITE_SHD, often in floats.
                    # Do not infer that EVENT_WRITE_SHD ends every XDK call.
                    xdk_tail_context["last_event_write_shd"] += 1
                else:
                    xdk_tail_context["other_last_good_packet"] += 1
                if metadata and int(metadata[2]) > 0:
                    # These are draws ALREADY parsed before failure; accepting
                    # the damaged suffix or automatically replaying the
                    # prefix would risk incorrect ordering/double submission.
                    xdk_tail_context["samples_with_draw_prefix"] += 1
                    xdk_tail_context["draws_in_rejected_sample_prefixes"] += int(metadata[2])
                last_end, failure_address = (LAST_GOOD_END.search(line),
                                             FAIL_PACKET.search(line))
                if last_end and failure_address:
                    distance = int(failure_address[1], 16) - int(last_end[1], 16)
                    if 0 <= distance <= 256:
                        xdk_tail_context["fail_distance_from_last_good_end_" +
                                         str(distance) + "_bytes"] += 1
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
        owned_fetch = FETCH_REJECT_OWNER.search(line)
        if owned_fetch:
            kind, slot, vs, ps = owned_fetch.groups()
            invalid_fetch_shader_owners[
                f"{kind.lower()}/slot={slot}/vs={vs.lower()}/ps={(ps or 'n/a').lower()}"
            ] += 1
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
        resolves = RESOLVE_STATS.search(rest)
        if resolves:
            latest_frame["resolve_stats"] = {
                "clears": int(resolves[1]), "resolves": int(resolves[2]),
                "skipped": int(resolves[3]),
            }
        gpu = GPU_COST.search(rest)
        if gpu:
            latest_frame["gpu_ms"] = {"average": float(gpu[1]), "max": float(gpu[2])}
        copies = BUFFER_COST.search(rest)
        if copies:
            latest_frame["buffer_copies_per_frame"] = {
                "average": float(copies[1]), "max": int(copies[2]),
                "guest_kb_copied": int(copies[3]),
            }
        cpu = CPU_COST.search(rest)
        if cpu:
            latest_frame["render_cpu_ms"] = float(cpu[1])
            latest_frame["gpu_wait_ms"] = float(cpu[2])
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
    # Group shader failures by static, decoded CF and directly executed
    # instruction words. Never publish or persist the shader bytes themselves.
    groups: dict[tuple, list[tuple[str, list[int], str, str, str, str]]] = {}
    unclassified = 0
    for item in shader_family_inputs:
        shader_hash, words, origin_kind, address, packet, call = item
        decoded = shader_static_cf(words)
        if decoded is None:
            unclassified += 1
            continue
        used, execs, bound = decoded
        key = (len(words), bound, execs, used, tuple(words[i] for i in used))
        groups.setdefault(key, []).append(item)
    shader_families = []
    for key, members in groups.items():
        size, bound, execs, used, _ = key
        differing = [i for i in range(size)
                     if any(m[1][i] != members[0][1][i] for m in members[1:])]
        shader_families.append({
            "word_count": size,
            "failure_count": len(members),
            "cf_pair_count": bound,
            "direct_exec_ranges": [list(r) for r in execs],
            "executed_word_count_including_cf": len(used),
            "varying_word_indices": differing,
            "varying_words_outside_direct_exec": all(i not in used for i in differing),
            "shader_hashes": [m[0] for m in members],
            "load_kinds": sorted(set(m[2] for m in members)),
            "source_calls": sorted(set(m[5] for m in members)),
            "note": "Direct-CF-only classification; not proof of correct translator semantics.",
        })
    shader_families.sort(key=lambda x: (-x["failure_count"], x["word_count"]))
    return {
        "shader_analysis_families": shader_families,
        "shader_failures_without_static_cf": unclassified,
        "issue_messages_observed": {key: counts[key] for key in ISSUES},
        "pm4_miss_samples": dict(sorted(rt_misses.items())),
        "fetch_writer_samples": dict(sorted(writers.items())),
        "invalid_fetch_shader_owner_samples": dict(sorted(invalid_fetch_shader_owners.items())),
        "incomplete_xdk_draw_samples": dict(sorted(xdk_draw_samples.items())),
        "pm4_first_failure_sample_sites": dict(sorted(xdk_failure_sites.items())),
        "pm4_impossible_last_good_header_samples": dict(
            sorted(malformed_previous_headers.items())),
        "incomplete_xdk_tail_context_samples": dict(sorted(xdk_tail_context.items())),
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
            "EVENT_WRITE_SHD before rejected XDK tail is provenance only: it is not a verified end-of-call marker, recovery delimiter, or permission to replay partial draws.",
            "A common static shader CF prefix does not identify the access-violation instruction or prove shader correctness.",
            "GPU/Resolve/copy timings and draws are frame-window samples, not the cost of a single building collapse.",
            "Skipping resolves just because their counts are high can break the game's original post-processing.",
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
