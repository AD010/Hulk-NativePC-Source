#!/usr/bin/env python3
"""Install read-only VdSwap frame-frequency probe into our private ReXGlue SDK build.

This does not rewrite guest time, add a limiter, or change rendering.
It counts actual XDK VdSwap calls per second. Supports the SDK 'recomp'
source version, refusing unsupported source layouts to avoid bad patches.
"""
from __future__ import annotations

import pathlib
import sys

INCLUDE_ANCHOR = "#include <atomic>\n"
CVAR_ANCHOR = "void VdSwap_entry(mapped_void buffer_ptr,"
PATCH_MARKER = "HULK_FRAME_PROBE_V1"
CVAR = '''// HULK_FRAME_PROBE_V1: reads true guest swaps, not driver FPS estimates.
REXCVAR_DEFINE_BOOL(hulk_frame_probe, false, "Hulk/Diagnostics",
                    "Log one-second XDK VdSwap frequency; no FPS manipulation");

'''
PRELUDE = '''  // HULK_FRAME_PROBE_V1: wall-clock XDK swap count. The VdSwap export is the
  // common game/guest-to-GPU boundary, independent from host presentation.
  if (REXCVAR_GET(hulk_frame_probe)) {
    using Clock = std::chrono::steady_clock;
    static std::mutex probe_mutex;
    static Clock::time_point last = Clock::now();
    static uint32_t swaps = 0;
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(probe_mutex);
    ++swaps;
    const auto span = std::chrono::duration<double>(now - last).count();
    if (span >= 1.0) {
      REXKRNL_INFO("[HULK/FPS] VdSwap rate={:.2f}/s, total={} in {:.3f}s",
                   double(swaps) / span, swaps, span);
      swaps = 0;
      last = now;
    }
  }
'''

def patch_sdk(root: pathlib.Path) -> None:
    path = root / "src/kernel/xboxkrnl/xboxkrnl_video.cpp"
    original = path.read_text(encoding="utf-8-sig")
    if PATCH_MARKER in original:
        print("Hulk guest VdSwap probe already installed")
        return
    if original.count(INCLUDE_ANCHOR) != 1:
        raise ValueError("Unexpected SDK includes; no patch performed")
    updated = original.replace(INCLUDE_ANCHOR,
        '#include <chrono>\n#include <mutex>\n' + INCLUDE_ANCHOR, 1)
    if updated.count(CVAR_ANCHOR) != 1:
        raise ValueError("Expected one VdSwap exported function")
    updated = updated.replace(CVAR_ANCHOR, CVAR + CVAR_ANCHOR, 1)
    marker = "  // All of these parameters are REQUIRED.\n"
    if updated.count(marker) != 1:
        raise ValueError("Unexpected VdSwap body; no patch performed")
    updated = updated.replace(marker, PRELUDE + marker, 1)
    path.write_text(updated, encoding="utf-8", newline="\n")
    print("Installed read-only Hulk VdSwap timing probe")

def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: patch_sdk_hulk_fps_probe.py SDK_ROOT", file=sys.stderr)
        return 2
    try:
        patch_sdk(pathlib.Path(sys.argv[1]))
        return 0
    except (OSError, ValueError) as exc:
        print(f"Hulk VdSwap probe: {exc}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
