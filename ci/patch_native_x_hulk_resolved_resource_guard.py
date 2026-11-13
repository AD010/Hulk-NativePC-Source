#!/usr/bin/env python3
"""Source-only Hulk NativeX resolved-texture Vulkan image contract guard.

Some GPU resolves bypass the existing GuestTextures descriptor validator and
reach ResolvedTextures::GetOrCreate directly. Reject invalid guest and host
image dimensions rather than inventing substitute game data.
"""
from __future__ import annotations
import os
from pathlib import Path
import subprocess
import sys

PIN = os.getenv("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
PINS = {"bf965e952c98e050753e91d08f32685a9f754b12",
        "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}

INCLUDE_OLD = "#include <algorithm>\n#include <set>\n"
INCLUDE_NEW = "#include <algorithm>\n#include <atomic>\n#include <set>\n"
GUEST_OLD = """  ApplyInvalidations();
  if (cube) slices = 6;
  Key key{"""
GUEST_NEW = """  ApplyInvalidations();
  // HULK/RESOLVE-RESOURCE-GUARD: do not let bogus Xenos FETCH metadata
  // create an invalid Vulkan image (log 176: rectangular 1280x720 cube).
  // No clamping or synthetic replacement pixels: preserve exact game data.
  const bool hulk_invalid_guest =
      !width || !height || width > 32768u || height > 32768u ||
      (!cube && (!slices || slices > 2048u)) ||
      (cube && width != height);
  if (hulk_invalid_guest) {
    static std::atomic<uint64_t> hulk_rejected_guest{0};
    const uint64_t n = hulk_rejected_guest.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 16u || n % 4096u == 0u)
      REXLOG_WARN("[HULK/RESOLVE-RESOURCE-GUARD] guest #{} cube={} {}x{} layers={}",
                  n, cube, width, height, slices);
    return nullptr;
  }
  if (cube) slices = 6;
  Key key{"""
HOST_OLD = """  desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
  device_->CreateTexture(desc, nullptr, &texture.texture);"""
HOST_NEW = """  desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
  // An anisotropic or excessive host render scale can invalidate an
  // otherwise valid guest image. Check the actual host allocation as well.
  const bool hulk_invalid_host =
      !desc.Width || !desc.Height || desc.Width > 32768u ||
      desc.Height > 32768u || (cube && desc.Width != desc.Height);
  if (hulk_invalid_host) {
    static std::atomic<uint64_t> hulk_rejected_host{0};
    const uint64_t n = hulk_rejected_host.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 16u || n % 4096u == 0u)
      REXLOG_WARN("[HULK/RESOLVE-RESOURCE-GUARD] host #{} cube={} guest={}x{} host={}x{}",
                  n, cube, width, height, desc.Width, desc.Height);
    textures_.erase(key);
    return nullptr;
  }
  device_->CreateTexture(desc, nullptr, &texture.texture);"""

def patch_text(source: str) -> tuple[str, bool]:
    if all(x in source for x in (INCLUDE_NEW, GUEST_NEW, HOST_NEW)):
        return source, False
    if "HULK/RESOLVE-RESOURCE-GUARD" in source:
        raise ValueError("Partial Hulk resolved Vulkan image guard")
    for old in (INCLUDE_OLD, GUEST_OLD, HOST_OLD):
        if source.count(old) != 1:
            raise ValueError("Pinned NativeX source anchor drift: " + old[:64])
    for old, new in ((INCLUDE_OLD, INCLUDE_NEW),
                     (GUEST_OLD, GUEST_NEW),
                     (HOST_OLD, HOST_NEW)):
        source = source.replace(old, new, 1)
    return source, True

# Allocation is not a completed resolve. An early return after GetOrCreate
# (for example an invalid destination slice) leaves last_resolve at zero.
LOOKUP_OLD = "  return newest && !newest->invalidated ? newest : nullptr;"
LOOKUP_NEW = "  return newest && newest->last_resolve && !newest->invalidated ? newest : nullptr;"

def patch_lookup(source: str) -> tuple[str, bool]:
    if source.count(LOOKUP_NEW) == 2 and LOOKUP_OLD not in source:
        return source, False
    if source.count(LOOKUP_OLD) != 2 or LOOKUP_NEW in source:
        raise ValueError("Pinned NativeX completed-resolve lookup anchors drifted")
    return source.replace(LOOKUP_OLD, LOOKUP_NEW), True


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_resolved_resource_guard.py <NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    actual = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"],
                                      text=True).strip()
    if PIN not in PINS or actual != PIN:
        raise ValueError("Unexpected NativeX revision: " + actual)
    path = root / "src/targets.cpp"
    updated, changed = patch_text(path.read_text(encoding="utf-8"))
    updated, lookup_changed = patch_lookup(updated)
    changed = changed or lookup_changed
    if changed:
        path.write_text(updated, encoding="utf-8", newline="\n")
    print("Hulk resolved resource Vulkan descriptor guard:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
