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
// HULK/GUEST-FPS-CAP: Hulk-specific CVAR name avoids SDK's gpu_guest_fps_limit collision.
// Unlike SDK guest timing, this is a maximum XDK VdSwap pacing control.
// It limits the maximum frequency of guest submissions. It cannot make
// a 30Hz game submit 60Hz frames or fix game-specific rigid-body updates.
REXCVAR_DEFINE_INT32(hulk_guest_swap_fps_cap, 0, "Hulk/Performance",
                     "Maximum XDK VdSwap submits per second; 0 = no cap");

'''
FPS_PACER = '''  // HULK/GUEST-FPS-CAP: actual guest submission pacing. This does not
  // change the Xbox virtual clock or its physics timestep. If the PPC game
  // submits only at 30Hz, setting 60 or 120 won't synthesize new frames.
  {
    using Clock = std::chrono::steady_clock;
    const int32_t fps_limit = std::clamp(REXCVAR_GET(hulk_guest_swap_fps_cap), 0, 240);
    static std::mutex limiter_mutex;
    static Clock::time_point next_submit{};
    static int32_t previous_limit = -1;
    std::unique_lock<std::mutex> guard(limiter_mutex);
    if (!fps_limit) {
      previous_limit = 0;
      next_submit = Clock::time_point{};
    } else {
      const auto interval = std::chrono::duration_cast<Clock::duration>(
          std::chrono::duration<double>(1.0 / double(fps_limit)));
      const auto now = Clock::now();
      if (previous_limit != fps_limit ||
          next_submit.time_since_epoch().count() == 0 ||
          now > next_submit + interval * 2) {
        next_submit = now;
      }
      if (next_submit > now) std::this_thread::sleep_until(next_submit);
      next_submit = Clock::now() + interval;
      previous_limit = fps_limit;
    }
  }
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
        # A partially applied patch must not silently pass source preflight.
        required = (
            "REXCVAR_DEFINE_BOOL(hulk_frame_probe",
            "REXCVAR_DEFINE_INT32(hulk_guest_swap_fps_cap, 0",
            "std::clamp(REXCVAR_GET(hulk_guest_swap_fps_cap), 0, 240)",
            "std::this_thread::sleep_until(next_submit)",
            'REXKRNL_INFO("[HULK/FPS] VdSwap rate=',
        )
        if any(original.count(token) != 1 for token in required):
            raise ValueError("Partial or duplicated Hulk VdSwap probe patch")
        print("Hulk guest VdSwap probe already installed and verified")
        return
    if original.count(INCLUDE_ANCHOR) != 1:
        raise ValueError("Unexpected SDK includes; no patch performed")
    updated = original.replace(INCLUDE_ANCHOR,
        '#include <chrono>\n#include <mutex>\n#include <thread>\n' + INCLUDE_ANCHOR, 1)
    if updated.count(CVAR_ANCHOR) != 1:
        raise ValueError("Expected one VdSwap exported function")
    updated = updated.replace(CVAR_ANCHOR, CVAR + CVAR_ANCHOR, 1)
    marker = "  // All of these parameters are REQUIRED.\n"
    if updated.count(marker) != 1:
        raise ValueError("Unexpected VdSwap body; no patch performed")
    updated = updated.replace(marker, FPS_PACER + PRELUDE + marker, 1)
    path.write_text(updated, encoding="utf-8", newline="\n")
    print("Installed Hulk VdSwap probe and unique guest FPS cap")

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
