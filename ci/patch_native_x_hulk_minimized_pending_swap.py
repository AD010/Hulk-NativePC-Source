#!/usr/bin/env python3
"""Avoid Vulkan Present of queued swap-tag frames while Hulk's window is hidden.

The existing D125 minimized-window fix in ExecutePresent is insufficient when
PresentPending is called BEFORE ExecutePresent's UpdateSize check, and on
renderer-thread wakeups. Keep the hidden frame's FinishHiddenFrame path; only
discard stale display-only queued frames. No test code/runtime probes added.
Apply to both reviewed NativeXRenderer pins.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

PINS = {
    "bf965e952c98e050753e91d08f32685a9f754b12",
    "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e",
}

OLD = """void PresentPending(bool room = false) {
  if (!g_pending_count) return;
  uint64_t executed = g_swaps_executed.load(std::memory_order_acquire);"""

NEW = """void PresentPending(bool room = false) {
  if (!g_pending_count) return;
  // HULK/RETAIL-PRESENT-FOCUS: D125 handles hidden frames in ExecutePresent,
  // but this pending-swap path also runs before that check and on command-
  // queue wakeups. Do not acquire a back buffer or call Present while the
  // window has no client area (for example during Alt+Tab minimization).
  // Drop only display-only pending frames; the upcoming ExecutePresent will
  // still complete its GPU uploads via FinishHiddenFrame as before.
  if (!g_device->UpdateSize()) {
    g_pacing.skipped += g_pending_count;
    g_pending_count = 0;
    g_pending_swap.store(0, std::memory_order_release);
    return;
  }
  uint64_t executed = g_swaps_executed.load(std::memory_order_acquire);"""

D140_HEAD_OLD = """void PresentPending(bool room = false, bool check_time = true) {
  if (!g_pending_count) return;
  uint64_t executed = g_swaps_executed.load(std::memory_order_acquire);"""
D140_HEAD_NEW = """void PresentPending(bool room = false, bool check_time = true) {
  if (!g_pending_count) return;
  // HULK/RETAIL-PRESENT-FOCUS: D140 checks pending frames after nearly
  // every command. Preserve its fast path: query window size ONLY when a
  // pending frame is actually about to call ShowPendingFrame/Present.
  auto hulk_pending_visible = [&]() {
    if (g_device->UpdateSize()) return true;
    g_pacing.skipped += g_pending_count;
    g_pending_count = 0;
    g_pending_swap.store(0, std::memory_order_release);
    return false;
  };
  uint64_t executed = g_swaps_executed.load(std::memory_order_acquire);"""


def transform(source: str) -> str:
    if "HULK/RETAIL-PRESENT-FOCUS" in source:
        if source.count("HULK/RETAIL-PRESENT-FOCUS") != 1 or \
           "g_pending_swap.store(0, std::memory_order_release);" not in source:
            raise ValueError("Partial NativeX minimized Present fix")
        if D140_HEAD_NEW in source and source.count("if (!hulk_pending_visible()) return;") != 3:
            raise ValueError("Partial D140 minimized Present candidate fix")
        return source
    if "if (!g_device->UpdateSize()) {" not in source:
        raise ValueError("Missing existing D125 minimized-frame completion")
    if source.count(OLD) == 1:
        return source.replace(OLD, NEW, 1)
    if source.count(D140_HEAD_OLD) != 1:
        raise ValueError("No reviewed D135/D140 pending Present signature")
    first = source.index(D140_HEAD_OLD)
    last = source.index("\nvoid ExecutePresent(", first)
    body = source[first:last]
    if body.count("ShowPendingFrame(") != 3:
        raise ValueError("D140 pending-frame presentation paths changed")
    body = body.replace(D140_HEAD_OLD, D140_HEAD_NEW, 1)
    body = body.replace("    ShowPendingFrame(", "    if (!hulk_pending_visible()) return;\n    ShowPendingFrame(")
    return source[:first] + body + source[last:]

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_minimized_pending_swap.py <NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    pin = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
                         "bf965e952c98e050753e91d08f32685a9f754b12")
    if pin not in PINS:
        raise ValueError("Unreviewed NativeX pin")
    actual = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if actual != pin:
        raise ValueError(f"Wrong renderer checkout: {actual}")
    file = root / "src/renderer.cpp"
    original = file.read_text(encoding="utf-8")
    updated = transform(original)
    if updated != original:
        file.write_text(updated, encoding="utf-8", newline="\n")
    print("Hulk Release Vulkan pending swap minimized focus fix:",
          "applied" if updated != original else "already applied")

if __name__ == "__main__":
    main()
