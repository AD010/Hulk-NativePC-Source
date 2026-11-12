#!/usr/bin/env python3
"""Read-only NativeX pixel-program origin at exact captured XDK Draw.

Hulk log192: frame 2157 contains 49 pixel-program disagreements. The first
24 reported are consecutive XDK DrawIndexedVertices #1819..1842, all carrying
tracker PS e42552fce84cb8ac vs GPU 0dad3cb9972d38fd. This probe records
the real StateTracker Program::loaded_by/packet/address/word count at the
point a draw is captured. It NEVER selects/replaces a shader or skips a draw.
No guest bytecode is written to a log or repository.
"""
from pathlib import Path
import os
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
                     "bf965e952c98e050753e91d08f32685a9f754b12")
SUPPORTED = {"bf965e952c98e050753e91d08f32685a9f754b12",
             "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}
ANCHOR = "  c.programs[1] = tracker.pixel_program_ptr();"
HEADER = "#include <atomic>"
CXX = r"""  // HULK/PS-PROGRAM-ORIGIN: distinguish one missed PM4 IM_LOAD from
  // 49 independent failed draws. This is a bounded, READ-ONLY diagnostic:
  // no changes to c.programs, descriptor bindings, GPU state or scheduling.
  const StateTracker::Program* hulk_ps = c.programs[1];
  const uint64_t hulk_ps_hash = hulk_ps ? hulk_ps->hash : 0ull;
  static std::atomic<uint64_t> hulk_draw_samples{0};
  static std::atomic<uint64_t> hulk_program_changes{0};
  static std::atomic<uint64_t> hulk_last_ps_hash{~uint64_t(0)};
  const uint64_t hulk_capture_no =
      hulk_draw_samples.fetch_add(1, std::memory_order_relaxed) + 1;
  const bool hulk_changed =
      hulk_last_ps_hash.exchange(hulk_ps_hash, std::memory_order_relaxed) != hulk_ps_hash;
  const uint64_t hulk_change_no = hulk_changed
      ? hulk_program_changes.fetch_add(1, std::memory_order_relaxed) + 1
      : 0;
  // Log the first 128 real PS transitions plus rare samples of the two
  // conflicting shaders observed in the owner's frame-2157 verifier.
  const bool hulk_known_mismatch =
      hulk_ps_hash == 0xe42552fce84cb8acull ||
      hulk_ps_hash == 0x0dad3cb9972d38fdull;
  if ((hulk_changed && hulk_change_no <= 128) ||
      (hulk_known_mismatch && (hulk_capture_no % 2400 == 0))) {
    REXLOG_INFO("[HULK/PS-PROGRAM-ORIGIN] draw_capture={} "
                "ps={:016x} vs={:016x} load_call={} packet={:#x} "
                "address={:#x} words={} immediate={}",
                hulk_capture_no, hulk_ps_hash,
                c.programs[0] ? c.programs[0]->hash : 0ull,
                hulk_ps ? uint32_t(hulk_ps->loaded_by) : 0u,
                hulk_ps ? hulk_ps->packet : 0u,
                hulk_ps ? hulk_ps->address : 0u,
                hulk_ps ? hulk_ps->ucode.size() : size_t(0),
                hulk_ps && hulk_ps->immediate);
  }
"""

def patch(source: str) -> tuple[str, bool]:
    if "HULK/PS-PROGRAM-ORIGIN" in source:
        if source.count(CXX) != 1 or source.count(ANCHOR) != 1:
            raise ValueError("Partial Hulk pixel-program provenance patch")
        return source, False
    if source.count(ANCHOR) != 1:
        raise ValueError("NativeX CaptureDraw shader assignment drift")
    if HEADER not in source:
        # All previous Hulk patches are applied in preflight before this
        # one; some modify the standard include block. The NativeX renderer
        # always uses its local state tracker, so anchor after state.h.
        anchor = '#include "state.h"'
        if source.count(anchor) != 1:
            raise ValueError("NativeX StateTracker include anchor drift")
        source = source.replace(anchor, anchor + "\n" + HEADER, 1)
    return source.replace(ANCHOR, ANCHOR + "\n" + CXX, 1), True

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_ps_program_origin.py NativeXRoot")
    root = Path(sys.argv[1]).resolve()
    revision = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if revision != PIN or PIN not in SUPPORTED:
        raise ValueError("Unreviewed NativeX revision " + revision)
    path = root / "src/renderer.cpp"
    fixed, changed = patch(path.read_text(encoding="utf-8"))
    if changed:
        path.write_text(fixed, encoding="utf-8", newline="\n")
    print("Hulk read-only PS program capture provenance:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
