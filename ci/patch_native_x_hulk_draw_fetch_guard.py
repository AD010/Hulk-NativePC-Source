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

# Low-frequency real Xbox texture-fetch and mip/LOD provenance.
# All supported NativeX pins share RequestTextures and the draw_log cvar.
# D140 moved its lod_bias calculation into a different function, so probe
# around that D140-only statement would break the older source preflight.
PROBE_CVAR_ANCHOR = 'REXCVAR_DEFINE_INT32(native_x_renderer_draw_log, 0, "NativeXRenderer",'
PROBE_CVAR_NEW = """REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_mip_probe, false, "Hulk/Quality",
                    "Sample shader-used texture fetch dwords and native LOD bias for mip provenance");
""" + PROBE_CVAR_ANCHOR
PROBE_NEW = """
  // HULK/MIP-PROVENANCE: record only true shader-used texture fetch words.
  // This does not replace textures, alter LOD bias or fake Xbox mip levels.
  if (REXCVAR_GET(native_x_renderer_hulk_mip_probe)) {
    static std::atomic<uint64_t> hulk_mip_draws{0};
    const uint64_t n = hulk_mip_draws.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 1 || n % 500000u == 0) {
      const float hulk_mip_bias = sized->reduced
          ? std::log2(sized->scale.x / scale_.x) : 0.0f;
      const int32_t hulk_af_override = rex::cvar::Query<int32_t>("anisotropic_override");
      uint32_t reported = 0;
      for (uint32_t slot = 0; slot < 32u && reported < 8; ++slot) {
        if ((used_texture_mask & (uint32_t(1) << slot)) == 0u) continue;
        const auto fetch = regs.GetTextureFetch(slot);
        if (fetch.type != xenos::FetchConstantType::kTexture) continue;
        const uint32_t first = rg::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + 6u * slot;
        REXLOG_INFO("[HULK/MIP-PROVENANCE] draw={} slot={} dimension={} "
                    "target_scale={:.3f},{:.3f} native_scale={:.3f},{:.3f} "
                    "lod_bias={:.3f} af_override={} "
                    "fetch={:#x},{:#x},{:#x},{:#x},{:#x},{:#x}",
                    n, slot, uint32_t(fetch.dimension),
                    sized->scale.x, sized->scale.y, scale_.x, scale_.y,
                    hulk_mip_bias, hulk_af_override,
                    regs[first], regs[first + 1u], regs[first + 2u],
                    regs[first + 3u], regs[first + 4u], regs[first + 5u]);
        ++reported;
      }
    }
  }"""
TEXTURE_NEW += PROBE_NEW

def patch(src: str) -> tuple[str, bool]:
    done = all(x in src for x in (TEXTURE_NEW, VERTEX_NEW, PROBE_CVAR_NEW))
    if done:
        return src, False
    if any(x in src for x in (TEXTURE_NEW, VERTEX_NEW, PROBE_CVAR_NEW,
                              "HULK/NATIVEX-DRAW-FETCH-GUARD",
                              "HULK/NATIVEX-VERTEX-FETCH-GUARD",
                              "HULK/MIP-PROVENANCE")):
        raise ValueError("Partial Hulk NativeX draw fetch/mip provenance patch")
    anchors = (TEXTURE_OLD, VERTEX_OLD, PROBE_CVAR_ANCHOR)
    if any(src.count(old) != 1 for old in anchors):
        raise ValueError("NativeX draw fetch/LOD source anchors drifted")
    if src.count("#include <bit>\\n") != 1:
        raise ValueError("NativeX draw source <bit> include anchor drifted")
    return (src.replace("#include <bit>\\n", "#include <atomic>\\n#include <bit>\\n", 1)
               .replace(TEXTURE_OLD, TEXTURE_NEW, 1)
               .replace(VERTEX_OLD, VERTEX_NEW, 1)
               .replace(PROBE_CVAR_ANCHOR, PROBE_CVAR_NEW, 1)), True

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
