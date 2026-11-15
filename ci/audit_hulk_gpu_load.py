#!/usr/bin/env python3
"""Source-only tool: summarize NativeX GPU timings from owner-provided logs.

Does not assert DXC shader correctness, monitor utilization or real in-game
frame pacing. Log windows are 600-frame aggregates and might overlap or be
truncated. Never uploads input logs or original game contents.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import re

DRAW = re.compile(r"\[native_x_renderer\] renderer: frame (\d+): (\d+) draw calls,.*?GPU ([\d.]+) ms average, ([\d.]+) ms max over (\d+) frames")
AA = re.compile(r"\[native_x_renderer\] renderer: frame (\d+): antialiasing \(([^)]+)\) applied (\d+) times in (\d+) frames")
MEMORY = re.compile(r"\[native_x_renderer\] renderer: frame (\d+): device memory (\d+) MB used in (\d+) MB of pages")


def audit(lines: str, target_fps: int = 60) -> dict:
    if not 15 <= target_fps <= 240:
        raise ValueError("Target FPS must be 15..240")
    budget = 1000.0 / target_fps
    windows = []
    aa = []
    mem = []
    for line in lines.splitlines():
        m = DRAW.search(line)
        if m:
            frame, draw_calls, avg, peak, count = m.groups()
            entry = dict(frame=int(frame), last_frame_draw_calls=int(draw_calls),
                         gpu_average_ms=float(avg), gpu_peak_ms=float(peak),
                         sampled_frames=int(count))
            if entry["sampled_frames"] == 0 and entry["gpu_average_ms"] == 0 and entry["gpu_peak_ms"] == 0:
                continue  # startup before any GPU samples were measured
            if (not all(math.isfinite(entry[key]) and entry[key] >= 0
                        for key in ("gpu_average_ms", "gpu_peak_ms"))
                    or entry["gpu_peak_ms"] < entry["gpu_average_ms"]
                    or not 1 <= entry["sampled_frames"] <= 100_000):
                raise ValueError("Invalid NativeX GPU sample window")
            windows.append(entry)
        m = AA.search(line)
        if m:
            frame, mode, count, n = m.groups()
            aa.append(dict(frame=int(frame), mode=mode,
                           applications=int(count), sample_frames=int(n)))
        m = MEMORY.search(line)
        if m:
            frame, used, allocated = map(int, m.groups())
            mem.append(dict(frame=frame, used_mb=used, allocated_mb=allocated))
    if not windows:
        raise ValueError("No valid NativeX GPU timing windows; do not invent GPU utilization")
    # One record per frame. The latest duplicate supersedes older lines.
    by_frame = {row["frame"]: row for row in windows}
    windows = [by_frame[f] for f in sorted(by_frame)]
    frames = sum(w["sampled_frames"] for w in windows)
    average = sum(w["gpu_average_ms"] * w["sampled_frames"] for w in windows) / frames
    worst = max(windows, key=lambda w: w["gpu_peak_ms"])
    exceeded = [w for w in windows if w["gpu_average_ms"] > budget]
    return {
        "schema": "hulk_nativex_gpu_cost_v1", "target_fps": target_fps,
        "target_gpu_budget_ms": round(budget, 3),
        "gpu_average_weighted_ms": round(average, 3),
        "max_recorded_gpu_ms": worst["gpu_peak_ms"],
        "max_recorded_gpu_frame_window": worst["frame"],
        "windows": windows,
        "window_count_over_gpu_budget": len(exceeded),
        "sampled_frames_in_over_budget_windows": sum(w["sampled_frames"] for w in exceeded),
        "total_sampled_frames": frames,
        "largest_draw_calls_in_last_frame_of_window": max(w["last_frame_draw_calls"] for w in windows),
        "aa_applied_windows": aa,
        "memory_windows": mem,
        "gpu_utilization_percent_measured": False,
        "full_gameplay_fps_measured": False,
        "note": ("Numbers are NativeX GPU event timings in intermittent aggregation windows, "
                 "not Windows task-manager GPU load, not percent GPU busy, not "
                 "an end-to-end FPS trace. The draw count is only the LAST frame "
                 "of each sampling window. Heavy GPU time is not an argument "
                 "for blindly splitting Havok, lowering texture mip quality "
                 "or corrupting game-specific MSAA resolves."),
    }


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("log", type=Path)
    p.add_argument("--target-fps", type=int, default=60)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    if args.log.stat().st_size > (128 << 20):
        raise ValueError("Input log unreasonably large")
    report = audit(args.log.read_text(encoding="utf-8", errors="replace"), args.target_fps)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k not in
                      ("windows", "aa_applied_windows", "memory_windows", "note")}, indent=2))

if __name__ == "__main__":
    main()
