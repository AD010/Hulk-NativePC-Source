#!/usr/bin/env python3
"""Preserve stencil/depth resolves when Hulk 4x guest MSAA has pixel-exact host storage.

NativeX host targets are single-sample even when Xbox guest has 4x EDRAM
MSAA. The Hulk sample-grid path stores separate guest samples as full native
pixels. Scaled=True suppresses the separate packed depth+stencil resolve
unconditionally, even if the host source/destination rectangles coincide
pixel for pixel. Permit that original packing path ONLY for an exact, native,
single-sample depth view; all mismatched/aliased/scaled copies retain the
upstream rejection. No fabricated stencil values or new render targets.
"""
from pathlib import Path
import os
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
                     "bf965e952c98e050753e91d08f32685a9f754b12")
PINS = {"bf965e952c98e050753e91d08f32685a9f754b12",
        "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}

OLD = """  if (source->depth && target->read_packed && !scaled && !cube && source->stencil_srv) {"""
NEW = """  // HULK/DEPTH-STENCIL-GRID-PARITY: "scaled" can be true solely
  // because 640x360 4x guest MSAA uses a full-size native Vulkan image.
  // If BOTH host rectangles have exactly the same pixels, the existing
  // packed depth/stencil pass remains valid. The previous !scaled gate
  // omitted it, leaving stale stencil-dependent texture fetches.
  // Refuse real resampling, alternate EDRAM aliases, multi-sample host
  // sources, shadow/reduced targets, or unverified depth views.
  const bool hulk_depth_stencil_exact_pixels =
      REXCVAR_GET(native_x_renderer_hulk_expand_msaa_sample_grid) &&
      surface && surface.depth && surface.msaa() == 2 &&
      source && source->depth && source->samples == 1 &&
      !source->reduced && !aliased &&
      from.width > 0 && from.height > 0 &&
      from.x == to.x && from.y == to.y &&
      from.width == to.width && from.height == to.height;
  if (source->depth && target->read_packed &&
      (!scaled || hulk_depth_stencil_exact_pixels) &&
      !cube && source->stencil_srv) {"""

def patch_text(source: str) -> tuple[str, bool]:
    if source.count(NEW) == 1:
        return source, False
    if "HULK/DEPTH-STENCIL-GRID-PARITY" in source:
        raise ValueError("Partially applied Hulk depth/stencil pixel parity")
    if source.count(OLD) != 1:
        raise ValueError("Upstream NativeX stencil resolve gate changed")
    if "native_x_renderer_hulk_expand_msaa_sample_grid" not in source:
        raise ValueError("Hulk exact 4x host sample grid must be installed first")
    return source.replace(OLD, NEW, 1), True

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_depth_stencil_grid.py NativeXRoot")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN or PIN not in PINS:
        raise ValueError("Unreviewed pinned NativeX revision: " + sha)
    path = root / "src/renderer.cpp"
    result, modified = patch_text(path.read_text(encoding="utf-8"))
    if modified:
        path.write_text(result, encoding="utf-8", newline="\n")
    print("Hulk native pixel-exact depth/stencil resolve:",
          "installed" if modified else "already installed")

if __name__ == "__main__":
    main()
