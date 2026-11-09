#!/usr/bin/env python3
"""Hulk XDK6534: reconstruct GPU state written between D3D hooks.

The log 099 observer shows only 362/1397 exact state matches:
1035 wrong pixel programs and SQ_INTERPOLATOR_CNTL, and PM4 writes
outside hooked calls. DO NOT hardcode expected register/shader values.

Read the bounded gap in the same guest D3D command-buffer segment between
last tracked call and next call. Reuse NativeXRenderer's original PM4
ScanPackets+Gather which can handle IM_LOAD and SET_CONSTANT. Fail closed
on segment changes, unusually large gaps, or malformed PM4. This affects
native state tracking, not the host graphics backend.
"""
from pathlib import Path
import subprocess
import sys

import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")

HEADER_ANCHOR = """  uint32_t last_device_ = 0;  // the device of the last device call (for kPackets calls)
  bool skip_empty_calls_ = false;"""
HEADER_NEW = """  uint32_t last_device_ = 0;  // the device of the last device call (for kPackets calls)
  // Hulk's engine writes PM4/shader loads between the eight hooked D3D calls.
  // Only scan a monotonic, short gap in the same segment/device.
  uint32_t last_hulk_segment_end_ = 0;
  uint32_t last_hulk_segment_device_ = 0;
  bool last_hulk_segment_valid_ = false;
  bool skip_empty_calls_ = false;"""

RESET_ANCHOR = """  applied_to_ = 0;
  inline_begin_ = {};
}"""
RESET_NEW = """  applied_to_ = 0;
  inline_begin_ = {};
  last_hulk_segment_end_ = 0;
  last_hulk_segment_device_ = 0;
  last_hulk_segment_valid_ = false;
}"""

ENTER_ANCHOR = """  p.begin = Load32(base, p.device + d.command_buffer_write) + 4;
  p.scan_from = p.begin;
  p.vs_dirty = vs_dirty;"""
ENTER_NEW = """  p.begin = Load32(base, p.device + d.command_buffer_write) + 4;
  p.scan_from = p.begin;
  // Guest writes from *unhooked* D3D calls persist in the real Xenos GPU.
  // Existing tracked-call-only scanning misses their IM_LOAD shaders and
  // packet-only registers. The user's real GPU observer measured 1035
  // pixel program / SQ_INTERPOLATOR_CNTL mismatches in 1397 draws.
  if (last_hulk_segment_valid_ && last_hulk_segment_device_ == p.device &&
      p.begin > last_hulk_segment_end_ &&
      p.begin - last_hulk_segment_end_ <= 0x40000u &&
      (last_hulk_segment_end_ & 3u) == 0) {
    scan_.Clear();
    ScanPackets(base, last_hulk_segment_end_, p.begin,
                scan_, 0, false, true);
    if (scan_.complete) {
      if (!scan_.writes.empty() || !scan_.loads.empty() ||
          !scan_.shader_loads.empty()) {
        static std::atomic<uint32_t> reported{0};
        const uint32_t n = reported.fetch_add(1, std::memory_order_relaxed);
        if (n < 20) {
          REXLOG_INFO("[HULK/STATE-GAP] unhooked PM4 bytes={} writes={} shader_loads={} indirect={} draws={}",
                      p.begin - last_hulk_segment_end_, scan_.writes.size(),
                      scan_.shader_loads.size(), scan_.indirect, scan_.draws);
        }
        // Gather updates the state and immutable shader-program cache
        // before the next real draw snapshot is captured.
        Gather(p, call, false, base);
      }
    } else {
      static std::atomic<bool> warned{false};
      if (!warned.exchange(true)) REXLOG_WARN(
          "[HULK/STATE-GAP] Malformed/interrupted unhooked PM4 gap; skipped");
    }
  }
  p.vs_dirty = vs_dirty;"""

LEAVE_ANCHOR = """  Gather(p, call, draw, base);

  last_.call = call;"""
LEAVE_NEW = """  Gather(p, call, draw, base);
  last_hulk_segment_end_ = end;
  last_hulk_segment_device_ = p.device;
  last_hulk_segment_valid_ = true;

  last_.call = call;"""

def once(src: str, old: str, new: str) -> tuple[str,bool]:
    if src.count(new)==1:
        return src,False
    if src.count(old)!=1:
        raise ValueError("Hulk PM4 gap expected upstream anchor missing or duplicated")
    return src.replace(old,new),True

def patch_header(src: str) -> tuple[str,bool]:
    return once(src,HEADER_ANCHOR,HEADER_NEW)

def patch_cpp(src: str) -> tuple[str,bool]:
    changes=[]
    for old,new in ((RESET_ANCHOR,RESET_NEW),(ENTER_ANCHOR,ENTER_NEW),(LEAVE_ANCHOR,LEAVE_NEW)):
        src,changed=once(src,old,new)
        changes.append(changed)
    if any(changes) and not all(changes):
        raise ValueError("Partial PM4 gap tracker patch")
    return src,any(changes)

def main() -> None:
    if len(sys.argv)!=2:
        raise SystemExit("usage: patch_native_x_hulk_pm4_gap.py <pinned NativeXRenderer checkout>")
    root=Path(sys.argv[1]).resolve()
    sha=subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if sha!=PIN:
        raise ValueError("Expected pinned upstream "+PIN+" got "+sha)
    files=((root/"src/state.h",patch_header),(root/"src/state.cpp",patch_cpp))
    edits=[]
    for p,action in files:
        updated,changed=action(p.read_text(encoding="utf-8"))
        edits.append((p,updated,changed))
    if len({changed for _,_,changed in edits})!=1:
        raise ValueError("Partially installed state patch, refusing")
    for p,updated,changed in edits:
        if changed:p.write_text(updated,encoding="utf-8",newline="\n")
    print("Hulk unhooked PM4 gap scanner:", "installed" if edits[0][2] else "already installed")

if __name__=="__main__":
    main()
