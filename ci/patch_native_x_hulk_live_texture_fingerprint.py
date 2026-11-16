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
INCLUDES_OLD="#include <algorithm>\n#include <cstring>"
INCLUDES_NEW="#include <atomic>\n#include <algorithm>\n#include <cstring>\n#include <rex/cvar.h>"
CVAR_ANCHOR="namespace native_x_renderer {\n"
CVAR=String.raw if False else """REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_live_texture_id, false,
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
                      "guest_mip={:#x} gpu_base_bytes={} h0={:016x} h1={:016x} "
                      "(source fingerprint, no asset assumption)",
                      n, uint32_t(key.format), key.GetWidth(), key.GetHeight(),
                      hulk_base, uint64_t(key.mip_page) << 12,
                      hulk_base_bytes, hulk_fingerprint(0),
                      hulk_fingerprint(hulk_second_at));
        }
      }
    }
    base_hash = hash(key.base_page, hulk_base_bytes);
    load_base = host_texture.base_hash != base_hash;
  }"""

def patch(src: str) -> tuple[str,bool]:
    if "HULK/LIVE-XENOS-ARC-IDENTITY" in src:
        if (src.count(BODY_NEW)!=1 or src.count(CVAR)!=1 or
            src.count(INCLUDES_NEW)!=1):
            raise ValueError("Partially installed source texture identity bridge")
        return src,False
    for name,old in (("header",INCLUDES_OLD),("namespace",CVAR_ANCHOR),
                     ("guest resident",BODY_OLD)):
        if src.count(old)!=1:
            raise ValueError(f"NativeX D140 guest textures {name} anchor drift")
    src=src.replace(INCLUDES_OLD,INCLUDES_NEW,1)
    src=src.replace(CVAR_ANCHOR,CVAR+"\n"+CVAR_ANCHOR,1)
    src=src.replace(BODY_OLD,BODY_NEW,1)
    return src,True

def main() -> None:
    if len(sys.argv)!=2:
        raise SystemExit("Usage: patch_native_x_hulk_live_texture_fingerprint.py NativeXRoot")
    root=Path(sys.argv[1]).resolve()
    revision=subprocess.check_output(
        ["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if revision != PIN or revision not in PINS:
        raise ValueError("Unexpected NativeX revision "+revision)
    target=root/"src"/"guest_textures.cpp"
    source,changed=patch(target.read_text(encoding="utf-8"))
    if changed:target.write_text(source,encoding="utf-8",newline="\n")
    print("Hulk TRUE GPU texture source identity probe:",
          "installed" if changed else "already installed")

if __name__=="__main__":main()
