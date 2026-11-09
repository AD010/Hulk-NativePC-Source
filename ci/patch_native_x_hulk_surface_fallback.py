#!/usr/bin/env python3
"""Recover lost Hulk native Vulkan PM4 draws caused by strict RT identity.

Retains per-packet surfaces when known; falls back to actual XDK-bound surfaces
for otherwise rejected draws. No game data, guest shaders or emulated graphics.
"""
from pathlib import Path
import subprocess
import sys

import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
CVAR="REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_surface_fallback, true, \"NativeXRenderer\",\n                    \"Recover Hulk's unmatched PM4 Vulkan draws with genuine bound XDK RT/depth objects. \"\n                    \"Strict packet-time mapping remains first; no Xenos graphics fallback.\");\n"
DECL_ANCHOR="// Resolve synthetic PM4 draws using the GPU register image at each packet,"
DRAW_OLD="  synthetic.device = device;\n  std::array<GuestSurface, 5> packet_surfaces{};\n  if (!HulkPacketSurfaces(tracker, base, device, packet_surfaces)) return;\n  CaptureDraw(synthetic, tracker, base, &packet_surfaces);"
DRAW_NEW="  synthetic.device = device;\n  std::array<GuestSurface, 5> packet_surfaces{};\n  if (HulkPacketSurfaces(tracker, base, device, packet_surfaces)) {\n    CaptureDraw(synthetic, tracker, base, &packet_surfaces);\n  } else if (REXCVAR_GET(native_x_renderer_hulk_surface_fallback)) {\n    // Runtime 146: strict mapping dropped at least 250k guest PM4 draws.\n    // Reuse the previously rendered native path only with real bound XDK objects.\n    bool has_bound_surface = false;\n    for (uint32_t slot = 0; slot < 5; ++slot)\n      has_bound_surface |= bool(ReadBoundSurface(base, device, slot));\n    if (!has_bound_surface) return;\n    CaptureDraw(synthetic, tracker, base);\n    static std::atomic<uint64_t> fallbacks{0};\n    const uint64_t n = ++fallbacks;\n    if (n == 1 || n % 10000 == 0)\n      REXLOG_WARN(\"[HULK/PM4-SURFACE] recovered native Vulkan Draw with bound XDK object; total={}\", n);\n  } else {\n    return;\n  }"

def patch_text(source):
    done=(CVAR in source,DRAW_NEW in source)
    if all(done): return source,False
    if any(done): raise ValueError("Partial Hulk PM4 surface fallback patch")
    if source.count(DECL_ANCHOR)!=1 or source.count(DRAW_OLD)!=1:
        raise ValueError("NativeX packet-time surface anchor mismatch")
    return source.replace(DECL_ANCHOR,CVAR+"\n"+DECL_ANCHOR,1).replace(DRAW_OLD,DRAW_NEW,1),True

def main():
    if len(sys.argv)!=2: raise SystemExit("usage: patch_native_x_hulk_surface_fallback.py <NativeX source>")
    root=Path(sys.argv[1]).resolve()
    rev=subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if rev!=PIN:raise ValueError("NativeX pinned revision mismatch "+rev)
    target=root/"src/renderer.cpp"
    after,changed=patch_text(target.read_text(encoding="utf8"))
    if changed:target.write_text(after,encoding="utf8",newline="\n")
    print("Hulk: unmatched PM4 Vulkan Draw fallback", "installed" if changed else "already installed")
if __name__=="__main__":main()
