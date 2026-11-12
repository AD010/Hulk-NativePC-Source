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

  last_.call = call;"""
PATCH = """  // HULK/INCOMPLETE-XDK-DRAW-GUARD: a truncated or malformed PM4 call
  // must never update shader programs or register state from a partial
  // scan. 7 calls were incompletely read in the log185 verifier. Returning
  // no TrackedCall skips this unreliable draw instead of leaking an invalid
  // program to subsequent frames or the NVIDIA shader compiler.
  if (!scan_.complete) {
    static std::atomic<uint64_t> hulk_incomplete_xdk{0};
    const uint64_t count = hulk_incomplete_xdk.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 16 || count % 10000 == 0)
      REXLOG_WARN("[HULK/INCOMPLETE-XDK-DRAW] #{} call={} was_draw={} - discarded partial PM4 state",
                  count, uint32_t(call), draw);
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

def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_incomplete_xdk_draw.py NativeXRoot")
    root = Path(sys.argv[1]).resolve()
    actual = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if PIN not in PINS or actual != PIN:
        raise ValueError("Unreviewed NativeX source SHA " + actual)
    target = root / "src/state.cpp"
    output, changed = patch(target.read_text(encoding="utf-8"))
    if changed:
        target.write_text(output, encoding="utf-8", newline="\n")
    print("Hulk incomplete XDK PM4 draw fail-closed guard:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
