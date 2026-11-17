#!/usr/bin/env python3
"""Bridge actual NativeX guest texture bytes to original EoR asset evidence.

This source patch is intentionally a READ-ONLY, disabled-by-default
provenance probe, NOT a replacement texture binder.  It runs inside the
actual GuestTextures::LoadTextureDataFromResidentMemoryImpl path, once per
load request, and publishes two 128-byte fingerprints over true resident
tiled Xenos guest memory, with format and dimensions.  The original ARC
fingerprint catalog must corroborate these before we may substitute a
source image/extra high-res mips.  No GPU writes or fake fetch constants.
"""
import os
from pathlib import Path
import subprocess
import sys

PIN=os.environ.get("HULK_NATIVEX_SOURCE_SHA",
    "bf965e952c98e050753e91d08f32685a9f754b12")
PINS={"bf965e952c98e050753e91d08f32685a9f754b12",
      "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e",
      "fa19ea85fec5c7f7f8a2df82d6c4a86548817b52"}
INCLUDES_OLD="#include <algorithm>\n#include <atomic>\n#include <cstring>"
INCLUDES_NEW="#include <algorithm>\n#include <atomic>\n#include <cstring>\n#include <rex/cvar.h>"
CVAR_ANCHOR="namespace native_x_renderer {\n"
CVAR= """REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_live_texture_id, false,
                    "Hulk/Original Texture Proof",
                    "Read-only actual Xbox guest texture format, sizes and "
                    "two 128-byte Xenos tiled-content fingerprints at first GPU base loads");
"""
BODY_OLD="""  if (load_base) {
    base_hash = hash(key.base_page, host_texture.GetGuestBaseSize());
    load_base = host_texture.base_hash != base_hash;
  }"""
BODY_NEW="""  if (load_base) {
    const uint32_t hulk_base_bytes = host_texture.GetGuestBaseSize();
    base_hash = hash(key.base_page, hulk_base_bytes);
    // HULK/LIVE-XENOS-ARC-IDENTITY: fingerprint the true GPU source data
    // the ACTIVE NativeX D140 texture uploader consumes. Two separate
    // windows are checked against the original archived mip pixels.
    // No original game bytes are logged/copied or written to GPU.
    if (REXCVAR_GET(native_x_renderer_hulk_live_texture_id)) {
      constexpr uint64_t kGpuMemorySize = 0x20000000ull;
      const uint64_t hulk_base = uint64_t(key.base_page) << 12;
      if (hulk_base < kGpuMemorySize &&
          hulk_base_bytes >= 128 &&
          hulk_base_bytes <= kGpuMemorySize - hulk_base) {
        const auto* hulk_bytes = memory_.memory().TranslatePhysical<const uint8_t*>(
            static_cast<uint32_t>(hulk_base));
        const uint32_t hulk_second_at =
            hulk_base_bytes >= 4224 ? 4096 : hulk_base_bytes - 128;
        auto hulk_fingerprint = [&](uint32_t offset) -> uint64_t {
          uint64_t value = UINT64_C(14695981039346656037);
          for (uint32_t i = 0; i < 128; ++i) {
            value ^= hulk_bytes[offset + i];
            value *= UINT64_C(1099511628211);
          }
          return value;
        };
        static std::atomic<uint64_t> hulk_samples{0};
        const uint64_t n = hulk_samples.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (n <= 48 || n % 12000 == 0) {
          REXLOG_INFO("[HULK/LIVE-XENOS-ARC-IDENTITY] source_read={} "
                      "format={} width={} height={} guest_base={:#x} "
                      "guest_mip={:#x} gpu_base_bytes={} full_xxh3={:016x} h0={:016x} h1={:016x} "
                      "(source fingerprint, no asset assumption)",
                      n, uint32_t(key.format), key.GetWidth(), key.GetHeight(),
                      hulk_base, uint64_t(key.mip_page) << 12,
                      hulk_base_bytes, base_hash, hulk_fingerprint(0),
                      hulk_fingerprint(hulk_second_at));
        }
      }
    }
    load_base = host_texture.base_hash != base_hash;
  }"""

# True guest SRV selection after dimension compatibility, not just texture upload.
# The full base XXH3 is the hash of the successfully uploaded guest data,
# unlike an address alone or a potentially stale sampled host image.

MIPS_OLD = """  if (load_mips) {
    mips_hash = hash(key.mip_page, host_texture.GetGuestMipsSize());
    load_mips = host_texture.mips_hash != mips_hash;
  }"""
MIPS_NEW = """  if (load_mips) {
    const uint32_t hulk_mips_bytes = host_texture.GetGuestMipsSize();
    mips_hash = hash(key.mip_page, hulk_mips_bytes);
    // HULK/LIVE-XENOS-MIP-IDENTITY: the actual separate GPU mip allocation.
    // Do not confuse this PACKED region with a single ARC mip level; its
    // physical per-level offsets still require separately verified layout.
    if (REXCVAR_GET(native_x_renderer_hulk_live_texture_id)) {
      constexpr uint64_t kGpuMemorySize = 0x20000000ull;
      const uint64_t hulk_mips_base = uint64_t(key.mip_page) << 12;
      if (hulk_mips_base < kGpuMemorySize &&
          hulk_mips_bytes >= 128 &&
          hulk_mips_bytes <= kGpuMemorySize - hulk_mips_base) {
        const auto* hulk_bytes = memory_.memory().TranslatePhysical<const uint8_t*>(
            static_cast<uint32_t>(hulk_mips_base));
        const uint32_t hulk_second_at =
            hulk_mips_bytes >= 4224 ? 4096 : hulk_mips_bytes - 128;
        auto hulk_fingerprint = [&](uint32_t offset) -> uint64_t {
          uint64_t value = UINT64_C(14695981039346656037);
          for (uint32_t i = 0; i < 128; ++i) {
            value ^= hulk_bytes[offset + i];
            value *= UINT64_C(1099511628211);
          }
          return value;
        };
        static std::atomic<uint64_t> hulk_mips_samples{0};
        const uint64_t n = hulk_mips_samples.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (n <= 48 || n % 12000 == 0) {
          REXLOG_INFO("[HULK/LIVE-XENOS-MIP-IDENTITY] source_read={} "
                      "format={} width={} height={} guest_mip={:#x} "
                      "packed_mip_bytes={} full_xxh3={:016x} h0={:016x} h1={:016x} "
                      "(packed GPU source; per-level ARC identity unproven)",
                      n, uint32_t(key.format), key.GetWidth(), key.GetHeight(),
                      hulk_mips_base, hulk_mips_bytes, mips_hash,
                      hulk_fingerprint(0), hulk_fingerprint(hulk_second_at));
        }
      }
    }
    load_mips = host_texture.mips_hash != mips_hash;
  }"""

GUEST_VIEW_OLD = """  auto* texture = static_cast<HostTexture*>(binding->texture);
  texture->MarkAsUsed();"""
GUEST_VIEW_NEW = """  auto* texture = static_cast<HostTexture*>(binding->texture);
  texture->MarkAsUsed();
  // HULK/GUEST-SHADER-IDENTITY: this is the guest GPU image selected by
  // the actual shader fetch, after dimension/resolve/placeholder handling.
  // Use the cache's verified uploaded base hash, not an untracked memory
  // reread. This probe never substitutes a GPU image or changes view reuse.
  if (REXCVAR_GET(native_x_renderer_hulk_live_texture_id) &&
      texture->base_hash.has_value() &&
      (is_signed && texture->signed_view() ? texture->signed_view() != nullptr
                                          : texture->view() != nullptr)) {
    static std::atomic<uint64_t> hulk_guest_bound{0};
    const uint64_t n = hulk_guest_bound.fetch_add(
        1, std::memory_order_relaxed) + 1;
    if (n <= 96 || n % 8192 == 0) {
      const auto& hulk_key = texture->key();
      REXLOG_INFO("[HULK/GUEST-SHADER-IDENTITY] selected={} "
                  "fetch={} guest_base={:#x} guest_mip={:#x} "
                  "format={} width={} height={} base_xxh3={:016x} "
                  "has_mips={} packed_mips_xxh3={:016x} signed={} "
                  "(real compatible guest SRV; not submitted Draw proof)",
                  n, fetch_constant, uint64_t(hulk_key.base_page) << 12,
                  uint64_t(hulk_key.mip_page) << 12, uint32_t(hulk_key.format),
                  hulk_key.GetWidth(), hulk_key.GetHeight(),
                  *texture->base_hash, texture->mips_hash.has_value(),
                  texture->mips_hash.value_or(0), is_signed);
    }
  }"""

def patch(src: str) -> tuple[str,bool]:
    if "HULK/LIVE-XENOS-ARC-IDENTITY" in src:
        if (src.count(BODY_NEW)!=1 or src.count(CVAR)!=1 or
            src.count(INCLUDES_NEW)!=1 or src.count(GUEST_VIEW_NEW)!=1 or
            src.count(MIPS_NEW)!=1 or BODY_OLD in src or MIPS_OLD in src or
            src.count(GUEST_VIEW_OLD)!=1):
            raise ValueError("Partially installed source texture identity bridge")
        return src,False
    for name,old in (("header",INCLUDES_OLD),("namespace",CVAR_ANCHOR),
                     ("guest resident",BODY_OLD),("guest packed mips",MIPS_OLD),
                     ("guest selected SRV",GUEST_VIEW_OLD)):
        if src.count(old)!=1:
            raise ValueError(f"NativeX D140 guest textures {name} anchor drift")
    src=src.replace(INCLUDES_OLD,INCLUDES_NEW,1)
    src=src.replace(CVAR_ANCHOR,CVAR+"\n"+CVAR_ANCHOR,1)
    src=src.replace(BODY_OLD,BODY_NEW,1)
    src=src.replace(MIPS_OLD,MIPS_NEW,1)
    src=src.replace(GUEST_VIEW_OLD,GUEST_VIEW_NEW,1)
    return src,True


# The guest upload alone is NOT a Draw binding: the same fetch constant can
# select a resolved render target or a placeholder through Textures::View.
# This second, read-only witness records the actual shader lookup result.
# No resource pointer, guest bytes, host texture or shader descriptor is replaced.
DRAW_INCLUDES_OLD = "#include <algorithm>\n#include <cmath>\n#include <vector>"
DRAW_INCLUDES_NEW = "#include <algorithm>\n#include <array>\n#include <atomic>\n#include <cmath>\n#include <vector>"
DRAW_LOGGING_OLD = "#include <rex/cvar.h>"
DRAW_LOGGING_NEW = "#include <rex/cvar.h>\n#include <rex/logging.h>"
DRAW_CVAR_OLD = "REXCVAR_DECLARE(int32_t, anisotropic_override);"
DRAW_CVAR_NEW = (DRAW_CVAR_OLD + "\n"
                 "REXCVAR_DECLARE(bool, native_x_renderer_hulk_live_texture_id);")
DRAW_BIND_OLD = """  Count(kept.value.kind);
  ++record_.counts[std::min<uint32_t>(kept.value.kind, 3)];
  return kept.value.view;"""
DRAW_BIND_NEW = """  // HULK/ACTIVE-SHADER-RESOURCE: actual NativeX shader-view selection.
  // 0=resolved host RT, 1=guest GPU memory texture, 2=placeholder,
  // 3=incompatible dimension. This is a lookup, NOT proof of GPU submission.
  // Correlate guest entries with HULK/LIVE-XENOS-ARC-IDENTITY by GPU address
  // and Xenos format; never treat resolved/placeholder as an ARC match.
  if (REXCVAR_GET(native_x_renderer_hulk_live_texture_id)) {
    const uint32_t hulk_kind = std::min<uint32_t>(kept.value.kind, 3u);
    static std::array<std::atomic<uint64_t>, 4> hulk_bound_counts{};
    const uint64_t hulk_seen =
        hulk_bound_counts[hulk_kind].fetch_add(1, std::memory_order_relaxed) + 1;
    if (hulk_seen <= 16 || hulk_seen % 12000 == 0) {
      REXLOG_INFO("[HULK/ACTIVE-SHADER-RESOURCE] kind={} count={} "
                  "fetch={} gpu_base={:#x} format={} dimension={} "
                  "(real shader view lookup, no Draw submit claim)",
                  hulk_kind, hulk_seen, fetch_constant,
                  uint64_t(fetch.base_address) << 12,
                  uint32_t(fetch.format), uint32_t(dimension));
    }
  }
  Count(kept.value.kind);
  ++record_.counts[std::min<uint32_t>(kept.value.kind, 3)];
  return kept.value.view;"""

# BF965/D127 pre-D140 sources do not carry D140's per-Draw count. The
# *single source-only patch-chain preflight* intentionally tests them first.
# Select by exact observed source shape, not by a permissive partial match.
DRAW_BIND_OLD_LEGACY = """  Count(kept.value.kind);
  return kept.value.view;"""
DRAW_BIND_NEW_LEGACY = DRAW_BIND_NEW.replace(
    "  ++record_.counts[std::min<uint32_t>(kept.value.kind, 3)];\n", "")

def patch_draw(src: str) -> tuple[str, bool]:
    # A partial installation is unsafe: the CVar must resolve uniquely and
    # the cached draw path must remain unchanged outside this observer.
    pairs = (("includes", DRAW_INCLUDES_OLD, DRAW_INCLUDES_NEW),
             ("logging", DRAW_LOGGING_OLD, DRAW_LOGGING_NEW),
             ("cvar", DRAW_CVAR_OLD, DRAW_CVAR_NEW))
    has_d140 = DRAW_BIND_OLD in src
    has_legacy = DRAW_BIND_OLD_LEGACY in src
    if has_d140 == has_legacy:
        # A source with two binding sites must never receive a guessed hook.
        if "HULK/ACTIVE-SHADER-RESOURCE" not in src:
            raise ValueError("Ambiguous NativeX source Draw binding")
    old, new = ((DRAW_BIND_OLD, DRAW_BIND_NEW) if has_d140 else
                (DRAW_BIND_OLD_LEGACY, DRAW_BIND_NEW_LEGACY))
    if "HULK/ACTIVE-SHADER-RESOURCE" in src:
        complete = (src.count(DRAW_BIND_NEW) == 1 or
                    src.count(DRAW_BIND_NEW_LEGACY) == 1)
        if not complete or (src.count(DRAW_CVAR_NEW) != 1 or
                            src.count(DRAW_INCLUDES_NEW) != 1 or
                            src.count(DRAW_LOGGING_NEW) != 1 or
                            src.count("HULK/ACTIVE-SHADER-RESOURCE") != 1):
            raise ValueError("Partially installed shader resource witness")
        # The anchored original return block is still expected once *inside*
        # the new witness; an extra unpatched block must be rejected.
        if (DRAW_BIND_NEW in src and src.count(DRAW_BIND_OLD) != 1 or
            DRAW_BIND_NEW_LEGACY in src and src.count(DRAW_BIND_OLD_LEGACY) != 1):
            raise ValueError("Duplicated original shader view binding")
        return src, False
    for name, before, after in pairs:
        if src.count(before) != 1 or after in src:
            raise ValueError(f"NativeX shader view {name} anchor drift")
    if src.count(old) != 1:
        raise ValueError("NativeX original shader resource return ABI changed")
    for _, before, after in pairs:
        src = src.replace(before, after, 1)
    return src.replace(old, new, 1), True

def main() -> None:
    if len(sys.argv)!=2:
        raise SystemExit("Usage: patch_native_x_hulk_live_texture_fingerprint.py NativeXRoot")
    root=Path(sys.argv[1]).resolve()
    revision=subprocess.check_output(
        ["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if revision != PIN or revision not in PINS:
        raise ValueError("Unexpected NativeX revision "+revision)
    target=root/"src"/"guest_textures.cpp"
    draw_target=root/"src"/"textures.cpp"
    # Validate both patch targets before changing either one.
    source,changed=patch(target.read_text(encoding="utf-8"))
    draw_source,draw_changed=patch_draw(draw_target.read_text(encoding="utf-8"))
    if changed:target.write_text(source,encoding="utf-8",newline="\n")
    if draw_changed:draw_target.write_text(draw_source,encoding="utf-8",newline="\n")
    print("Hulk TRUE GPU texture source identity probe:",
          "installed" if changed else "already installed",
          "| active shader view witness:",
          "installed" if draw_changed else "already installed")

if __name__=="__main__":main()
