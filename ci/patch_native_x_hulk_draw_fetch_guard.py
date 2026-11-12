#!/usr/bin/env python3
"""Fail closed on invalid GPU fetch descriptors USED by a NativeX draw.

Hulk log 185: all 590 rejected texture fetch descriptors had low type bits
0, 1 or 3; a valid Xenos texture descriptor is type 2. The old renderer
continued such draws with placeholders and attempted 1D widths up to millions.
A vertex shader also must not read malformed vertex fetch buffers. Never
fabricate replacement fetch state or alter legitimate descriptor contents.
"""
from pathlib import Path
import os
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
    "bf965e952c98e050753e91d08f32685a9f754b12")
PINS = {"bf965e952c98e050753e91d08f32685a9f754b12",
        "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}

TEXTURE_OLD = """  uint32_t used_texture_mask = vertex_shader->GetUsedTextureMaskAfterTranslation() |
                               (pixel_shader ? pixel_shader->GetUsedTextureMaskAfterTranslation() : 0);
  guest_textures_->RequestTextures(used_texture_mask);"""
TEXTURE_NEW = """  uint32_t used_texture_mask = vertex_shader->GetUsedTextureMaskAfterTranslation() |
                               (pixel_shader ? pixel_shader->GetUsedTextureMaskAfterTranslation() : 0);
  // HULK/NATIVEX-DRAW-FETCH-GUARD: NEVER rasterize with an invalid texture
  // descriptor actually referenced by a translated shader. All 590
  // invalid descriptors in log 185 had types 0, 1 or 3, not kTexture (2).
  // A placeholder is useful for unsupported *valid* textures, not for
  // interpreting random float/command bytes as a valid GPU texture.
  for (uint32_t slot = 0; slot < 32u; ++slot) {
    if ((used_texture_mask & (1u << slot)) == 0u) continue;
    const auto fetch = regs.GetTextureFetch(slot);
    if (fetch.type != xenos::FetchConstantType::kTexture ||
        (fetch.dimension == xenos::DataDimension::k1D &&
         (fetch.tiled || fetch.size_1d.width >= 16384u))) {
      static std::atomic<uint64_t> rejected{0};
      const uint64_t n = rejected.fetch_add(1, std::memory_order_relaxed) + 1;
      if (n <= 20 || (n % 10000) == 0) {
        REXLOG_WARN("[HULK/INVALID-TEXTURE-DRAW] #{} slot={} type={} dim={} word0={:#x}",
                    n, slot, uint32_t(fetch.type),
                    uint32_t(fetch.dimension), fetch.dword_0);
      }
      ++stats_.failed;
      return false;
    }
  }
  guest_textures_->RequestTextures(used_texture_mask);"""

VERTEX_OLD = """      xenos::xe_gpu_vertex_fetch_t fetch = regs.GetVertexFetch(i * 32 + bit);
      if (fetch.type != xenos::FetchConstantType::kVertex) continue;
      if (!first_vertex_address) first_vertex_address = fetch.address;
      memory_->RequestRange(fetch.address << 2, fetch.size << 2);"""
VERTEX_NEW = """      xenos::xe_gpu_vertex_fetch_t fetch = regs.GetVertexFetch(i * 32 + bit);
      // HULK/NATIVEX-VERTEX-FETCH-GUARD: invalid/empty vertex streams
      // must not produce giant triangles from unrelated guest memory.
      // Check the complete 64-bit range BEFORE any 32-bit shift.
      const uint64_t vertex_start = uint64_t(fetch.address) * 4ull;
      const uint64_t vertex_bytes = uint64_t(fetch.size) * 4ull;
      if (fetch.type != xenos::FetchConstantType::kVertex ||
          !vertex_bytes || vertex_start >= 0x20000000ull ||
          vertex_bytes > 0x20000000ull - vertex_start) {
        static std::atomic<uint64_t> rejected{0};
        const uint64_t n = rejected.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 20 || (n % 10000) == 0) {
          REXLOG_WARN("[HULK/INVALID-VERTEX-DRAW] #{} slot={} type={} base={:#x} words={}",
                      n, i * 32 + bit, uint32_t(fetch.type),
                      uint32_t(fetch.address), uint32_t(fetch.size));
        }
        ++stats_.failed;
        return false;
      }
      if (!first_vertex_address) first_vertex_address = fetch.address;
      memory_->RequestRange(uint32_t(vertex_start), uint32_t(vertex_bytes));"""

def patch(src: str) -> tuple[str, bool]:
    done = TEXTURE_NEW in src and VERTEX_NEW in src
    if done:
        return src, False
    if any(token in src for token in (
            TEXTURE_NEW, VERTEX_NEW, "HULK/NATIVEX-DRAW-FETCH-GUARD",
            "HULK/NATIVEX-VERTEX-FETCH-GUARD")):
        raise ValueError("Partial Hulk NativeX draw fetch guard")
    if src.count(TEXTURE_OLD) != 1 or src.count(VERTEX_OLD) != 1:
        raise ValueError("NativeX draw fetch register anchors drifted")
    if src.count("#include <bit>\n") != 1:
        raise ValueError("NativeX draw source <bit> include anchor drifted")
    return (src.replace("#include <bit>\n", "#include <atomic>\n#include <bit>\n", 1)
               .replace(TEXTURE_OLD, TEXTURE_NEW, 1)
               .replace(VERTEX_OLD, VERTEX_NEW, 1)), True

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_draw_fetch_guard.py NativeXRoot")
    root = Path(sys.argv[1]).resolve()
    revision = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if revision != PIN or PIN not in PINS:
        raise ValueError("Unsupported NativeX revision: " + revision)
    path = root / "src/draw.cpp"
    fixed, changed = patch(path.read_text(encoding="utf-8"))
    if changed:
        path.write_text(fixed, encoding="utf-8", newline="\n")
    print("Hulk NativeX shader-used fetch descriptor guards:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
