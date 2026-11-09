#!/usr/bin/env python3
"""Don't reject an entire PM4 draw for unused, never-bound MRT slots.

Xenos RB_COLOR_MASK may retain nonzero masks for color slots 1..3 when the
game has only ever bound RT0. A color write mask alone is not a render-target
binding. This patch still rejects ambiguous RT0, depth, and previously bound
MRT slots, and never synthesizes a GuestSurface.
"""
from pathlib import Path
import subprocess
import sys

import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
OLD = """    const GuestSurface bound = ReadBoundSurface(base, device, slot);
    if (bound && HulkSurfaceKey(bound.surface_info, bound.info) == gpu_key) {"""
NEW = """    const GuestSurface bound = ReadBoundSurface(base, device, slot);
    // HULK/UNBOUND-MRT: RB_COLOR_MASK enables writes, not RT attachment.
    // Hulk's XDK binds RT0 (and optionally depth), while inactive MRT 1..3
    // can retain stale nonzero GPU masks. A slot never bound in the genuine
    // XDK surface history cannot supply an actual render target.
    // Do not reject a valid RT0 draw just because such a slot is absent.
    if (slot > 0 && slot < 4 && !bound) {
      std::lock_guard<std::mutex> guard(g_hulk_surface_history_mutex);
      if (g_hulk_surface_history[slot].empty()) continue;
    }
    if (bound && HulkSurfaceKey(bound.surface_info, bound.info) == gpu_key) {"""

def patch_text(source):
    if NEW in source:
        return source, False
    if source.count(OLD) != 1:
        raise ValueError("Hulk PM4 unbound MRT exact anchor missing or duplicated")
    if "bool HulkPacketSurfaces(" not in source:
        raise ValueError("Hulk PM4 surface matching absent")
    return source.replace(OLD, NEW, 1), True

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_unbound_mrt.py <pinned NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"],
                                   text=True).strip()
    if sha != PIN:
        raise ValueError("NativeX upstream SHA mismatch: " + sha)
    target = root / "src/renderer.cpp"
    result, modified = patch_text(target.read_text(encoding="utf-8"))
    if modified:
        target.write_text(result, encoding="utf-8", newline="\n")
    print("Hulk PM4 unused-MRT matching:", "installed" if modified else "already installed")

if __name__ == "__main__":
    main()
