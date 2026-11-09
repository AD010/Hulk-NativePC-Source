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

import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
OLD_GUARD="  // PM4 indirect helpers with no color/depth target are not geometry.\n  // Keep tracker state for the next draw; don't enqueue a null-target draw.\n  bool has_surface = false;\n  for (uint32_t i = 0; i < 5; ++i) {\n    const GuestSurface s = ReadBoundSurface(base, device, i);\n    has_surface |= bool(s) && s.width && s.height;\n  }\n  if (!has_surface) {\n    static std::atomic<uint64_t> hulk_no_target_skipped{0};\n    const uint64_t n = ++hulk_no_target_skipped;\n    if (n == 1 || n % 10000 == 0)\n      REXLOG_INFO(\"[HULK/PM4-GUARD] skipped {} unbound indirect draws\", n);\n    return;\n  }\n  TrackedCall synthetic{};"
NEW_GUARD="  // Hulk 149: PM4 carries GPU-bound targets even when the later XDK\n  // device's surface-object shadow is empty. An early XDK-only guard\n  // discarded 520k valid/unknown packet draws before their GPU register\n  // identity and known real surface-history could even be checked.\n  // HulkPacketSurfaces below matches packet-time registers against genuine\n  // observed XDK objects; unknowns still fail closed in the fallback.\n  TrackedCall synthetic{};"
OLD_EXACT="  if (HulkPacketSurfaces(tracker, base, device, packet_surfaces)) {\n    CaptureDraw(synthetic, tracker, base, &packet_surfaces);"
NEW_EXACT="  if (HulkPacketSurfaces(tracker, base, device, packet_surfaces)) {\n    bool has_late_xdk_object = false;\n    for (uint32_t slot = 0; slot < 5; ++slot)\n      has_late_xdk_object |= bool(ReadBoundSurface(base, device, slot));\n    if (!has_late_xdk_object) {\n      static std::atomic<uint64_t> packet_only{0};\n      const uint64_t n = ++packet_only;\n      if (n <= 8 || n % 10000 == 0)\n        REXLOG_INFO(\"[HULK/PM4-REGISTER-ONLY] Vulkan draw restored from recorded \"\n                    \"GPU state and genuine XDK surface history; total={}\", n);\n    }\n    CaptureDraw(synthetic, tracker, base, &packet_surfaces);"

def patch_text(source):
    """Patch the actual integrated renderer; fail closed if its structure changed."""
    import re
    if NEW_GUARD in source and NEW_EXACT in source:
        return source, False
    if NEW_GUARD in source or NEW_EXACT in source:
        raise ValueError("Partial Hulk PM4 register-first repair")

    guard_matches = list(re.finditer(
        r"(?ms)^  (?:// PM4 indirect helpers[^\n]*\n)?"
        r"(?:  // Keep tracker state[^\n]*\n)?"
        r"  bool has_surface = false;\n"
        r".*?^  TrackedCall synthetic\{\};", source))
    guard_matches = [m for m in guard_matches
                     if m.group().count("hulk_no_target_skipped") == 2
                     and "if (!has_surface)" in m.group()
                     and "return;" in m.group()]
    if len(guard_matches) != 1:
        raise ValueError(
            f"PM4 guard mismatch: exact={source.count(OLD_GUARD)} "
            f"marker={source.count('hulk_no_target_skipped')} "
            f"candidates={len(guard_matches)}")
    old_guard = guard_matches[0].group()
    # Constrain the second edit to the exact packet identity capture path.
    capture_matches = list(re.finditer(
        r"(?m)^  if \(HulkPacketSurfaces\(tracker, base, device, packet_surfaces\)\) \{\n"
        r"    CaptureDraw\(synthetic, tracker, base, &packet_surfaces\);", source))
    if len(capture_matches) != 1:
        raise ValueError(
            f"PM4 packet capture mismatch: exact={source.count(OLD_EXACT)} "
            f"candidates={len(capture_matches)}")
    return (source.replace(old_guard, NEW_GUARD, 1)
                  .replace(capture_matches[0].group(), NEW_EXACT, 1)), True

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
