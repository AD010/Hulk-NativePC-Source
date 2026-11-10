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
    "empty_dma_draw": re.compile(r"\[HULK/EMPTY-DMA-INDEX\]"),
    "fetch_writer": re.compile(r"\[HULK/FETCH-WRITER\]"),
}
CUMULATIVE = {
    "pm4_ambiguous_binding": re.compile(r"\[HULK/PM4-SURFACE\] skipped ambiguous synthetic RT/depth binding; total=(\d+)"),
    "pm4_packet_draws_accepted": re.compile(r"\[HULK/PM4-ACCEPT\].*?total=(\d+)"),
}
RT_MISS = re.compile(r"\[HULK/PM4-MISS\] slot=(\d+) kind=(\w+) reason=([\w_]+)")
FETCH_WRITER = re.compile(r"\[HULK/FETCH-WRITER\].*?slot=(\d+) dword=(\d+) value=(0x[0-9a-fA-F]+).*?source=(\w+)")
FRAME_STATS = re.compile(r"\[native_x_renderer\] renderer: frame (\d+):\s*(.*)")
DRAWS = re.compile(r"(\d+) draw calls, (\d+) drawn, (\d+) without targets, (\d+) failed")
FPS = re.compile(r"frame interval\s+([\d.]+) ms average,\s+([\d.]+) ms max")
PIPELINES = re.compile(r"draws skipped: (\d+) while a shader was translated, (\d+) while their pipeline compiled")


def analyze(text: str) -> dict:
    counts: Counter[str] = Counter()
    rt_misses: Counter[str] = Counter()
    writers: Counter[str] = Counter()
    cumulative: dict[str, int] = {name: 0 for name in CUMULATIVE}
    latest_frame: dict[str, object] = {}
    frames_seen: set[int] = set()
    for line in text.splitlines():
        for name, pattern in ISSUES.items():
            if pattern.search(line):
                counts[name] += 1
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
        "cumulative_reported_totals": cumulative,
        "last_renderer_frame": latest_frame,
        "renderer_stat_frames_observed": sorted(frames_seen),
        "limitations": [
            "Some warnings are throttled; counts are logged samples, not total GPU errors.",
            "PM4-SURFACE total is a cumulative replay counter, not a count of distinct discarded draw objects.",
            "Same-frame scene, game area, camera and settings are needed to infer improvement between logs.",
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
