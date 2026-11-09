#!/usr/bin/env python3
"""Hulk 149: fix PM4 replay guard ordering.

The old prefilter read only the *late* XDK surface shadow, discarding over
520k PM4 draws before the packet-time GPU register binding code could
attempt to match prior *real* XDK surfaces. Retain strict guest identity.
No made-up surfaces, Xenos host drawing, or raster scaling.
"""
import subprocess
import sys
from pathlib import Path

PIN="bf965e952c98e050753e91d08f32685a9f754b12"
OLD_GUARD="  // PM4 indirect helpers with no color/depth target are not geometry.\n  // Keep tracker state for the next draw; don't enqueue a null-target draw.\n  bool has_surface = false;\n  for (uint32_t i = 0; i < 5; ++i) {\n    const GuestSurface s = ReadBoundSurface(base, device, i);\n    has_surface |= bool(s) && s.width && s.height;\n  }\n  if (!has_surface) {\n    static std::atomic<uint64_t> hulk_no_target_skipped{0};\n    const uint64_t n = ++hulk_no_target_skipped;\n    if (n == 1 || n % 10000 == 0)\n      REXLOG_INFO(\"[HULK/PM4-GUARD] skipped {} unbound indirect draws\", n);\n    return;\n  }\n  TrackedCall synthetic{};"
NEW_GUARD="  // Hulk 149: PM4 carries GPU-bound targets even when the later XDK\n  // device's surface-object shadow is empty. An early XDK-only guard\n  // discarded 520k valid/unknown packet draws before their GPU register\n  // identity and known real surface-history could even be checked.\n  // HulkPacketSurfaces below matches packet-time registers against genuine\n  // observed XDK objects; unknowns still fail closed in the fallback.\n  TrackedCall synthetic{};"
OLD_EXACT="  if (HulkPacketSurfaces(tracker, base, device, packet_surfaces)) {\n    CaptureDraw(synthetic, tracker, base, &packet_surfaces);"
NEW_EXACT="  if (HulkPacketSurfaces(tracker, base, device, packet_surfaces)) {\n    bool has_late_xdk_object = false;\n    for (uint32_t slot = 0; slot < 5; ++slot)\n      has_late_xdk_object |= bool(ReadBoundSurface(base, device, slot));\n    if (!has_late_xdk_object) {\n      static std::atomic<uint64_t> packet_only{0};\n      const uint64_t n = ++packet_only;\n      if (n <= 8 || n % 10000 == 0)\n        REXLOG_INFO(\"[HULK/PM4-REGISTER-ONLY] Vulkan draw restored from recorded \"\n                    \"GPU state and genuine XDK surface history; total={}\", n);\n    }\n    CaptureDraw(synthetic, tracker, base, &packet_surfaces);"

def patch_text(source):
    done=(NEW_GUARD in source, NEW_EXACT in source)
    if all(done): return source,False
    if any(done): raise ValueError("Partial Hulk PM4 register-only repair")
    if source.count(OLD_GUARD)!=1 or source.count(OLD_EXACT)!=1:
        raise ValueError("Unexpected pinned NativeX / preceding Hulk patch anchors")
    return source.replace(OLD_GUARD,NEW_GUARD,1).replace(OLD_EXACT,NEW_EXACT,1),True

def main():
    if len(sys.argv)!=2:raise SystemExit("Usage: patch_native_x_hulk_packet_first.py <NativeX checkout>")
    root=Path(sys.argv[1]).resolve()
    sha=subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if sha!=PIN:raise ValueError("Unexpected NativeX revision "+sha)
    p=root/"src/renderer.cpp"
    out,changed=patch_text(p.read_text(encoding="utf8"))
    if changed:p.write_text(out,encoding="utf8",newline="\n")
    print("Hulk PM4 register/history first:", "installed" if changed else "already installed")
if __name__=="__main__":main()
