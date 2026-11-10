#!/usr/bin/env python3
"""Recover verified Hulk 4x GPU attachments from real 1x XDK views.

Observed in incredible_hulk_167.log: RB_SURFACE_INFO=0x0A020280, bound
XDK RB_SURFACE_INFO=0x14000500, and RB_COLOR_INFO=0x400 on both.
The actual #48 log 171 also proved GPU depth pitch 520/4x matches a genuine
XDK 1024x1024 shadow-size depth target at pitch 1040/1x. This patch
reconstructs only verified 2x/4x representations of genuine bound objects.
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
    // HULK/PM4-SAMPLE-VIEW: GPU 4x samples occupy the same storage
    // as an XDK 1x object with twice the pitch and twice the dimensions.
    // Applies to the 640/1280 color *and* 520/1040 depth shadow views.
    // Never bridge different EDRAM bases, storage formats or object kinds.
    if (bound && (slot == 0 || slot == 4) &&
        g_profile->output_width == 1280 && g_profile->output_height == 720 &&
        bound.depth == (slot == 4) && bound.width >= 2 && bound.height >= 2 &&
        !(bound.width & 1u) && !(bound.height & 1u) &&
        bound.width / 2 <= (gpu_surface & 0x3FFFu) &&
        bound.pitch() == 2u * (gpu_surface & 0x3FFFu) && bound.msaa() == 0 &&
        (gpu_surface & 0x3FFFu) != 0 &&
        ((gpu_surface >> 16) & 3u) == 2u &&
        (gpu_info & 0x000F0FFFu) == (bound.info & 0x000F0FFFu)) {
      GuestSurface packet_view = bound;
      packet_view.surface_info = gpu_surface;
      packet_view.info = gpu_info;
      packet_view.width = bound.width / 2;
      packet_view.height = bound.height / 2;
      out[slot] = packet_view;
      HulkRememberSurface(packet_view, slot);
      has_target = true;
      static std::atomic<uint64_t> hulk_sample_views{0};
      const uint64_t n = ++hulk_sample_views;
      if (n <= 12 || n % 10000 == 0)
        REXLOG_INFO("[HULK/PM4-SAMPLE-VIEW] #{} slot={} real XDK object={:#x} "
                    "matches GPU {}/{} 4x EDRAM base={} format={}",
                    n, slot, bound.object, packet_view.width, packet_view.height,
                    gpu_info & 0xFFFu, (gpu_info >> 16) & 0xFu);
      continue;
    }
    // HULK/PM4-RT0-REBASE: real Hulk GPU packets select EDRAM base 0
    // while the last XDK-bound color object still selects base 1024.
    // The RT0 pitch/format/sample grid remains a verified 640x360/4x
    // representation. Preserve the packet's actual base (never reuse the
    // XDK base), with either observed genuine 1x or 4x XDK dimensions.
    // Log 173: GPU surface 0x0A020280 / info 0x000, XDK info 0x400.
    if (slot == 0 && bound && !bound.depth &&
        g_profile->output_width == 1280 && g_profile->output_height == 720 &&
        gpu_surface == 0x0A020280u && gpu_info == 0u &&
        bound.info == 0x400u && bound.pitch() == bound.width &&
        ((bound.surface_info == 0x14000500u &&
          bound.width == 1280 && bound.height == 720 && bound.msaa() == 0) ||
         (bound.surface_info == gpu_surface &&
          bound.width == 640 && bound.height == 360 && bound.msaa() == 2))) {
      GuestSurface packet_view = bound;
      packet_view.surface_info = gpu_surface;
      packet_view.info = gpu_info;
      packet_view.width = 640;
      packet_view.height = 360;
      out[slot] = packet_view;
      HulkRememberSurface(packet_view, slot);
      has_target = true;
      static std::atomic<uint64_t> hulk_rt0_rebased{0};
      const uint64_t n = ++hulk_rt0_rebased;
      if (n <= 12 || n % 10000 == 0)
        REXLOG_INFO("[HULK/PM4-RT0-REBASE] #{} real XDK object={:#x} "
                    "packet EDRAM base={} guest={}x{} 4x",
                    n, bound.object, packet_view.edram_base(),
                    packet_view.width, packet_view.height);
      continue;
    }
    // HULK/PM4-HISTORY-ALIAS: when temporarily unbound, find the
    // genuine historical XDK surface by the exact paired 1x pitch,
    // EDRAM base, format and depth/color kind. Recheck its live XDK header
    // to avoid mistaking a reused object address for an old target.
    if ((slot == 0 || slot == 4) &&
        g_profile->output_width == 1280 && g_profile->output_height == 720 &&
        (gpu_surface & 0x3FFFu) != 0 &&
        2u * (gpu_surface & 0x3FFFu) <= 0x3FFFu &&
        ((gpu_surface >> 16) & 3u) == 2u) {
      GuestSurface verified{};
      {
        std::lock_guard<std::mutex> guard(g_hulk_surface_history_mutex);
        const HulkSurfaceIdentity xdk_key{2u * (gpu_surface & 0x3FFFu), gpu_info & 0x000F0FFFu};
        const auto it = g_hulk_surface_history[slot].find(xdk_key);
        if (it != g_hulk_surface_history[slot].end()) {
          const GuestSurface& known = it->second;
          const GuestSurface live = known ? ReadSurface(base, known.object, known.depth,
                                                       g_profile->objects) : GuestSurface{};
          if (live && live.depth == (slot == 4) &&
              live.surface_info == known.surface_info && live.info == known.info &&
              live.width == known.width && live.height == known.height &&
              live.width >= 2 && live.height >= 2 &&
              !(live.width & 1u) && !(live.height & 1u) &&
              live.width / 2 <= (gpu_surface & 0x3FFFu) &&
              live.pitch() == 2u * (gpu_surface & 0x3FFFu) && live.msaa() == 0 &&
              (live.info & 0x000F0FFFu) == (gpu_info & 0x000F0FFFu))
            verified = live;
        }
      }
      if (verified) {
        GuestSurface packet_view = verified;
        packet_view.surface_info = gpu_surface;
        packet_view.info = gpu_info;
        packet_view.width = verified.width / 2;
        packet_view.height = verified.height / 2;
        out[slot] = packet_view;
        HulkRememberSurface(packet_view, slot);
        has_target = true;
        static std::atomic<uint64_t> history_aliases{0};
        const uint64_t n = ++history_aliases;
        if (n <= 12 || n % 10000 == 0)
          REXLOG_INFO("[HULK/PM4-HISTORY-ALIAS] #{} slot={} authentic_object={:#x} "
                      "edram={} format={} samples=4",
                      n, slot, verified.object, packet_view.edram_base(), packet_view.format());
        continue;
      }
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
