#!/usr/bin/env python3
"""Hulk NativeX: validate guest resource dimensions before Vulkan allocation.

Log 176 (build #57) proves an invalid cube texture 1280x720 made it all
the way to DiligentCore. This is a structural descriptor contract, not a
guessed texture replacement: prevent invalid VkImage creation and fall back
through NativeX's existing ordinary missing-texture binding path.
Supports both pinned D127 and older NativeX source; no game data required.
"""
from __future__ import annotations
import os
from pathlib import Path
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
                     "bf965e952c98e050753e91d08f32685a9f754b12")
PINS = {"bf965e952c98e050753e91d08f32685a9f754b12",
        "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}
INCLUDE_OLD = "#include <algorithm>\n#include <cstring>"
INCLUDE_NEW = "#include <algorithm>\n#include <atomic>\n#include <cstring>"
DESCRIPTOR_OLD = """  desc.Width = key.GetWidth();
  desc.Height = key.GetHeight();
  desc.MipLevels = key.mip_max_level + 1;
  desc.Format = host_format.format;"""
DESCRIPTOR_NEW = """  desc.Width = key.GetWidth();
  desc.Height = key.GetHeight();
  desc.MipLevels = key.mip_max_level + 1;
  // HULK/TEXTURE-RESOURCE-GUARD: The game's FETCH state is sometimes
  // corrupted in packet replay (log176: a 1280x720 cube, format 6).
  // A cube must be square, array/depth cannot be empty and mip levels
  // cannot exceed the largest extent's full chain. Never invoke Diligent
  // with an impossible VkImageCreateInfo or substitute invented game data.
  // Missing textures use the existing native placeholder binding instead.
  const uint32_t hulk_extent = std::max(
      {desc.Width, desc.Height,
       key.dimension == xenos::DataDimension::k3D ? uint32_t(desc.Depth) : 1u});
  uint32_t hulk_max_mips = 0;
  for (uint32_t v = hulk_extent; v; v >>= 1) ++hulk_max_mips;
  const bool hulk_invalid_desc =
      !desc.Width || !desc.Height || !depth_or_array_size ||
      // HULK/FETCH-EXTENT-LIMIT: Xenos 1D register corruption has produced
      // widths 4,186,624 and 5,891,328 in the owner's actual logs. Without
      // explicit bounds these can pass the mip-chain check and reach Vulkan.
      // Reject, never silently clamp original texture addresses or sizes.
      desc.Width > 32768u || desc.Height > 32768u ||
      depth_or_array_size > 2048u ||
      (key.dimension == xenos::DataDimension::kCube && desc.Width != desc.Height) ||
      !desc.MipLevels || desc.MipLevels > hulk_max_mips;
  if (hulk_invalid_desc) {
    static std::atomic<uint64_t> hulk_invalid_textures{0};
    const uint64_t n = ++hulk_invalid_textures;
    if (n <= 16 || n % 4096 == 0)
      REXLOG_WARN("[HULK/TEXTURE-RESOURCE-GUARD] rejected #{} dimension={} "
                  "extent={}x{}x{} mips={} max_mips={} (no GPU allocation)",
                  n, uint32_t(key.dimension), desc.Width, desc.Height,
                  depth_or_array_size, desc.MipLevels, hulk_max_mips);
    return nullptr;
  }
  desc.Format = host_format.format;"""

def patch_text(source: str) -> tuple[str,bool]:
    if DESCRIPTOR_NEW in source and INCLUDE_NEW in source:
        return source, False
    if ("HULK/TEXTURE-RESOURCE-GUARD" in source or
        DESCRIPTOR_NEW in source or INCLUDE_NEW in source):
        raise ValueError("Partial NativeX Hulk texture guard; refusing")
    if source.count(INCLUDE_OLD) != 1 or source.count(DESCRIPTOR_OLD) != 1:
        raise ValueError("Pinned NativeX guest texture resource anchor changed")
    return source.replace(INCLUDE_OLD, INCLUDE_NEW, 1).replace(
        DESCRIPTOR_OLD, DESCRIPTOR_NEW, 1), True

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_texture_resource_guard.py <NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    actual = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"],
                                     text=True).strip()
    if PIN not in PINS or actual != PIN:
        raise ValueError("Unexpected NativeX source SHA " + actual)
    path = root / "src/guest_textures.cpp"
    result, changed = patch_text(path.read_text(encoding="utf-8"))
    if changed:
        path.write_text(result, encoding="utf-8", newline="\n")
    print("Hulk Vulkan guest texture descriptor validation:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
