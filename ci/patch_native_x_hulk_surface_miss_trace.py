#!/usr/bin/env python3
"""Hulk log160: categorize packet-time RT/depth identity misses by actual slot.

No renderer behavior changes. The existing verified GPU descriptor must match
a real XDK surface history entry; log why a synthetic PM4 draw is rejected.
"""
from pathlib import Path
import subprocess
import sys

PIN = "bf965e952c98e050753e91d08f32685a9f754b12"
OLD = "    unresolved = true;\n  }\n  if (!has_target || unresolved) {"
NEW = "    // Hulk 160: group genuine surface-key misses by attachment, without\n    // inventing or substituting any RT/depth object. Flicker / missing geometry\n    // can then be correlated with exact packet-time GPU identities.\n    static std::array<std::atomic<uint64_t>, 5> hulk_slot_misses{};\n    const uint64_t misses = ++hulk_slot_misses[slot];\n    if (misses <= 8 || misses % 20000 == 0)\n      REXLOG_WARN(\"[HULK/PM4-MISS] slot={} gpu_surface={:#x} gpu_info={:#x} \"\n                  \"bound={} bound_surface={:#x} bound_info={:#x} total={}\",\n                  slot, gpu_surface, gpu_info, bool(bound),\n                  bound ? bound.surface_info : 0u, bound ? bound.info : 0u, misses);\n    unresolved = true;\n  }\n  if (!has_target || unresolved) {"

def patch_text(source):
    if NEW in source: return source, False
    if source.count(OLD) != 1 or source.count("bool HulkPacketSurfaces(") != 1:
        raise ValueError("Unexpected native Vulkan per-slot surface matcher")
    return source.replace(OLD, NEW, 1), True

def main():
    if len(sys.argv) != 2: raise SystemExit("usage: patch_native_x_hulk_surface_miss_trace.py <NativeX>")
    checkout = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN: raise ValueError("NativeX SHA pin mismatch")
    p = checkout / "src" / "renderer.cpp"
    result, changed = patch_text(p.read_text(encoding="utf-8"))
    if changed: p.write_text(result, encoding="utf-8", newline="\n")
    print("Hulk PM4 real attachment-miss tracing:", "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
