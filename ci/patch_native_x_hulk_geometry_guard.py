#!/usr/bin/env python3
"""Hulk NativeX: do not submit invalid PM4 helper packets as scene geometry.

Log 124 showed hundreds of thousands of replays of unhooked PM4 packets and
PrimitiveProcessor failures for types 0, 48 and 49. This patch leaves all
real hooked XDK draws unchanged, does not change guest indices or shaders, and
skips only clearly invalid synthetic PM4 geometry. It logs the rejection.
"""
from pathlib import Path
import subprocess
import sys

import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
PATCHES = {
 "src/renderer.cpp": [
  (
   "  TrackedCall synthetic{};",
   "  // The ring contains predicated/helper packets which are not complete\n"
   "  // XDK draw calls. Do not feed invalid topologies into PrimitiveProcessor.\n"
   "  // This applies only to synthetic unhooked PM4 draws, never real hooks.\n"
   "  uint32_t hulk_initiator = 0;\n"
   "  // Unlike CaptureInlineData, this callback has no local rg namespace alias.\n"
  "  if (tracker.LastWrite(rex::graphics::XE_GPU_REG_VGT_DRAW_INITIATOR, hulk_initiator)) {\n"
   "    const uint32_t topology = hulk_initiator & 0x3fu;\n"
   "    if (topology == 0 || topology == 48 || topology == 49) {\n"
   "      static std::atomic<uint64_t> rejected{0};\n"
   "      const uint64_t n = ++rejected;\n"
   "      if (n <= 12 || n % 1000 == 0)\n"
   "        REXLOG_WARN(\"[HULK/GEOMETRY-GUARD] rejected synthetic invalid\"\n"
   "                    \" PM4 topology={} initiator={:#x}; total={}\", topology,\n"
   "                    hulk_initiator, n);\n"
   "      return;\n"
   "    }\n"
   "  }\n"
   "  TrackedCall synthetic{};"
  )
 ]
}

def transform(source, rules):
    flags = [new in source for old, new in rules]
    if all(flags):
        return source, False
    if any(flags):
        raise ValueError("Partial Hulk invalid topology PM4 guard")
    for old, new in rules:
        if source.count(old) != 1:
            raise ValueError("Hulk NativeX synthetic capture anchor missing")
        source = source.replace(old, new)
    return source, True

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_geometry_guard.py <NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN:
        raise ValueError("NativeX pin changed: " + sha)
    target = root / "src/renderer.cpp"
    after, modified = transform(target.read_text(encoding="utf-8"), PATCHES["src/renderer.cpp"])
    if modified:
        target.write_text(after, encoding="utf-8", newline="\n")
    print("Hulk synthetic PM4 invalid geometry guard:", "installed" if modified else "already installed")

if __name__ == "__main__":
    main()
