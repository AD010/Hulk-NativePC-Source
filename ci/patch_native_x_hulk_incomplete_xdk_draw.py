#!/usr/bin/env python3
"""Do not submit a partially decoded XDK PM4 draw into Hulk NativeX.

Normal StateTracker::Leave used to call Gather even if ScanPackets had marked
the packet stream incomplete. The owner log185 GPU verifier reported seven
incompletely read calls, alongside 58 pixel-program mismatches. The
relationship is not proven, but partial packet state must not poison GPU
shader/register state or be passed to the Vulkan renderer.
"""
from pathlib import Path
import os
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
                     "bf965e952c98e050753e91d08f32685a9f754b12")
PINS = {"bf965e952c98e050753e91d08f32685a9f754b12",
        "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}
ANCHOR = """  Gather(p, call, draw, base);
  last_hulk_segment_end_ = end;
  last_hulk_segment_device_ = p.device;
  last_hulk_segment_valid_ = true;

  last_.call = call;"""
PATCH = """  // HULK/INCOMPLETE-XDK-DRAW-GUARD: a truncated or malformed PM4 call
  // must never update shader programs or register state from a partial
  // scan. Hulk log191 showed the old broad guard dropping
  // Call::kPresent, resulting in one shown frame,
  // 39k replayed PM4 draws and 335 dynamic-heap exhaustion failures.
  // HULK/PRESENT-NOT-DRAW: Hulk log191 proves call=38 (Call::kPresent)
  // has an incomplete trailing PM4 scan while the actual XDK Present is
  // valid. Previously this guard returned nullptr for EVERY incomplete
  // call, silently dropping Present and leaving Vulkan's dynamic heap
  // without swap/fence reclamation. Preserve the real frame boundary
  // without replaying a single untrusted register or shader write.
  if (!scan_.complete && call == Call::kPresent) {
    static std::atomic<uint64_t> hulk_present_recovered{0};
    const uint64_t n = hulk_present_recovered.fetch_add(
        1, std::memory_order_relaxed) + 1;
    if (n <= 16 || n % 600 == 0)
      REXLOG_WARN("[HULK/PRESENT-FRAME-RECOVERY] #{} incomplete PM4 tail; "
                  "preserved original XDK Present without replaying partial writes", n);
    last_hulk_segment_valid_ = false;
    // HULK/XDK-BIN-TRANSACTION: an incomplete XDK packet scan did
    // not commit. Undo every BIN_MASK/SELECT change made by the
    // rejected call, including scans at nested KickSegment boundaries.
    scan_.bin_mask = p.hulk_bin_mask_at_enter;
    scan_.bin_select = p.hulk_bin_select_at_enter;
    scan_.Clear();
    last_.call = call;
    last_.draw = false;
    last_.device = p.device;
    last_.begin = GpuAddress(p.begin);
    last_.end = GpuAddress(end);
    last_.scan = &scan_;
    return &last_;
  }
  // Other incomplete XDK calls are still rejected, preserving the old
  // safety behavior; only Present's frame boundary is mandatory even
  // when the PM4 tail could not be decoded.
  if (!scan_.complete) {
    static std::atomic<uint64_t> hulk_incomplete_xdk{0};
    const uint64_t count = hulk_incomplete_xdk.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 16 || count % 10000 == 0) {
      // HULK/LOG192-PM4-BOUNDS: 40,000 XDK DrawIndexedVertices calls
      // rejected in four minutes. Attribute each rejection to a changed
      // segment pointer vs a well-bounded but malformed packet stream.
      // No game bytes or source-memory contents are logged.
      const bool scan_range_valid =
          end >= p.scan_from && end - p.scan_from < 0x400000u;
      REXLOG_WARN("[HULK/INCOMPLETE-XDK-DRAW] #{} call={} was_draw={} "
                  "begin={:#x} scan_from={:#x} end={:#x} segment_kick={} "
                  "range_ok={} bytes={} packet_draws={} indirect={} "
                  "writes={} loads={} shader_loads={} - discarded",
                  count, uint32_t(call), draw,
                  p.begin, p.scan_from, end, p.kick, scan_range_valid,
                  scan_range_valid ? end - p.scan_from : 0u,
                  scan_.draws, scan_.indirect, scan_.writes.size(),
                  scan_.loads.size(), scan_.shader_loads.size());
    }
    // Invalidate the unhooked-gap cursor too: never replay this damaged
    // segment on the next D3D call after rejecting its tail.
    last_hulk_segment_valid_ = false;
    // HULK/INCOMPLETE-INLINE-DRAW: a held BeginVertices may have armed
    // TakeCommittedInlineDraw(). If we discard its PM4 state but leave
    // inline_begin_.pending/end set, the next call can publish the previous
    // TrackedCall with stale shaders and index data. Cancel that deferred draw.
    inline_begin_.pending = false;
    inline_begin_.end = 0;
    // HULK/XDK-BIN-TRANSACTION: Clear intentionally preserves the
    // bin predicate for valid calls. On a rejected XDK call it MUST be
    // restored from Enter, otherwise future PM4 draws inherit bad BINs.
    scan_.bin_mask = p.hulk_bin_mask_at_enter;
    scan_.bin_select = p.hulk_bin_select_at_enter;
    scan_.Clear();
    return nullptr;
  }
""" + ANCHOR

def patch(source: str) -> tuple[str, bool]:
    if "HULK/INCOMPLETE-XDK-DRAW-GUARD" in source:
        if source.count(PATCH) != 1:
            raise ValueError("Partial XDK incomplete draw guard")
        return source, False
    if source.count(ANCHOR) != 1 or \
       "StateTracker::Leave(Call call, const uint8_t* base, bool nested)" not in source:
        raise ValueError("NativeX XDK Leave/Gather anchor drift")
    return source.replace(ANCHOR, PATCH, 1), True

# XDK Enter may be followed by a nested segment Kick before Leave.
# Checkpoint the entire outer call rather than the last individual segment.
PENDING_HEADER_OLD = """    uint64_t fetch_dirty = 0;
  };

  // The registers a finished call changed"""
PENDING_HEADER_NEW = """    uint64_t fetch_dirty = 0;
    // HULK/XDK-BIN-TRANSACTION: the actual beginning of this XDK call.
    uint64_t hulk_bin_mask_at_enter = 0xFFFFFFFFull;
    uint64_t hulk_bin_select_at_enter = 0xFFFFFFFFull;
  };

  // The registers a finished call changed"""

ENTER_OLD = """  p.fetch_dirty = fetch_dirty;
  scan_.Clear();"""
ENTER_NEW = """  p.fetch_dirty = fetch_dirty;
  // HULK/XDK-BIN-TRANSACTION: preserve the state at the beginning of
  // this call, before any nested segment scan can update BIN registers.
  p.hulk_bin_mask_at_enter = scan_.bin_mask;
  p.hulk_bin_select_at_enter = scan_.bin_select;
  scan_.Clear();"""


def patch_header(src: str) -> tuple[str, bool]:
    if PENDING_HEADER_NEW in src:
        return src, False
    if "HULK/XDK-BIN-TRANSACTION" in src:
        raise ValueError("Partial XDK predicate transaction header")
    if src.count(PENDING_HEADER_OLD) != 1:
        raise ValueError("XDK Pending header anchor mismatch")
    return src.replace(PENDING_HEADER_OLD, PENDING_HEADER_NEW, 1), True


def patch_enter(src: str) -> tuple[str, bool]:
    if ENTER_NEW in src:
        return src, False
    if src.count(ENTER_OLD) != 1:
        raise ValueError("XDK Enter checkpoint anchor mismatch")
    return src.replace(ENTER_OLD, ENTER_NEW, 1), True


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_incomplete_xdk_draw.py NativeXRoot")
    root = Path(sys.argv[1]).resolve()
    actual = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if PIN not in PINS or actual != PIN:
        raise ValueError("Unreviewed NativeX source SHA " + actual)
    state_cpp = root / "src/state.cpp"
    state_h = root / "src/state.h"
    cpp, header = (state_cpp.read_text(encoding="utf-8"),
                   state_h.read_text(encoding="utf-8"))
    enter, changed_enter = patch_enter(cpp)
    result, changed_guard = patch(enter)
    hdr, changed_header = patch_header(header)
    if len({changed_enter, changed_guard, changed_header}) != 1:
        raise ValueError("Mixed XDK predicate transaction / draw guard patch; refusing write")
    if changed_enter:
        state_cpp.write_text(result, encoding="utf-8", newline="\n")
        state_h.write_text(hdr, encoding="utf-8", newline="\n")
    print("Hulk incomplete XDK PM4 fail-closed + BIN rollback:",
          "installed" if changed_enter else "already installed")

if __name__ == "__main__":
    main()
