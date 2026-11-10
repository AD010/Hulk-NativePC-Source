#!/usr/bin/env python3
"""Recover a Hulk 640x360/4x GPU attachment from a real 1280x720/1x XDK view.

Observed in incredible_hulk_167.log: RB_SURFACE_INFO=0x0A020280, bound
XDK RB_SURFACE_INFO=0x14000500, and RB_COLOR_INFO=0x400 on both.
This is a constrained reconstruction of the GPU's real view of an existing
surface, not creation of a new guest object or speculative RT substitution.
"""
from pathlib import Path
import os
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
OLD = """    const HulkSurfaceIdentity gpu_key = HulkSurfaceKey(gpu_surface, gpu_info);
    const GuestSurface bound = ReadBoundSurface(base, device, slot);"""
NEW = """    const HulkSurfaceIdentity gpu_key = HulkSurfaceKey(gpu_surface, gpu_info);
    const GuestSurface bound = ReadBoundSurface(base, device, slot);
    // HULK/PM4-SAMPLE-VIEW: Hulk's 4x scene is addressed as 640x360 in
    // GPU packets while an earlier real XDK object may still expose the
    // physically equivalent 1280x720/1x output view. Never use this
    // bridge for different EDRAM bases, formats, sample counts or pitches.
    if (bound && (slot == 0 || slot == 4) &&
        g_profile->output_width == 1280 && g_profile->output_height == 720 &&
        bound.width == 1280 && bound.height == 720 &&
        bound.pitch() == 1280 && bound.msaa() == 0 &&
        (gpu_surface & 0x3FFFu) == 640u &&
        ((gpu_surface >> 16) & 3u) == 2u &&
        (gpu_info & 0x000F0FFFu) == (bound.info & 0x000F0FFFu)) {
      GuestSurface packet_view = bound;
      packet_view.surface_info = gpu_surface;
      packet_view.info = gpu_info;
      packet_view.width = 640;
      packet_view.height = 360;
      out[slot] = packet_view;
      HulkRememberSurface(packet_view, slot);
      has_target = true;
      static std::atomic<uint64_t> hulk_sample_views{0};
      const uint64_t n = ++hulk_sample_views;
      if (n <= 12 || n % 10000 == 0)
        REXLOG_INFO("[HULK/PM4-SAMPLE-VIEW] #{} slot={} real XDK object={:#x} "
                    "matches GPU 640x360/4x EDRAM base={} format={}",
                    n, slot, bound.object, gpu_info & 0xFFFu, (gpu_info >> 16) & 0xFu);
      continue;
    }"""

def patch_text(source: str):
    if NEW in source:
        return source, False
    if source.count(OLD) != 1 or source.count("bool HulkPacketSurfaces(") != 1:
        raise ValueError("Hulk per-packet XDK surface matcher anchor mismatch")
    return source.replace(OLD, NEW, 1), True

def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_sample_grid_packet_binding.py <NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN:
        raise ValueError("NativeXRenderer revision mismatch: " + sha)
    file = root / "src/renderer.cpp"
    new, changed = patch_text(file.read_text(encoding="utf-8"))
    if changed:
        file.write_text(new, encoding="utf-8", newline="\n")
    print("Hulk real packet-time 4x surface view:", "patched" if changed else "already patched")

if __name__ == "__main__":
    main()
