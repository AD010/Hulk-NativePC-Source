#!/usr/bin/env python3
"""Hulk 6534 NativeX front-buffer Resolve fallback.

The user's log 079 proves 31,800 completed NativeX presents and valid
3D Draw / Resolve work, but a BLACK final surface. NativeX upstream
PresentFrontBuffer only performs a lookup at the front object's fetch
address. If that address is wrong/missing, it clears the whole screen.

This strictly pinned, build-time source patch enables an opt-out,
small, Hulk-specific color Resolve fallback. It chooses the latest
valid, non-shadow, non-cube color Resolve, prioritizing a guest texture
that matches the game's output size, and keeps its identity swizzle.
It only runs if the normal front lookup fails and never changes guest
PowerPC, the original XDK hooks, RawInput, shader compiler or textures.
It is an experimental repair, not proof of visual correctness.
"""
from pathlib import Path
import subprocess
import sys

PIN = "bf965e952c98e050753e91d08f32685a9f754b12"
H_ANCHOR = """  ResolvedTexture* FindNewest(uint32_t address, bool cube);"""
H_NEW = """  // Hulk: best recent non-shadow, non-cube color Resolve when the front
  // object's guest fetch address does not match a captured Resolve.
  ResolvedTexture* FindNewestPresentable(uint32_t wanted_width, uint32_t wanted_height,
                                         uint64_t max_frame_age);
""" + H_ANCHOR
CPP_ANCHOR = "ResolvedTexture* ResolvedTextures::FindNewest(uint32_t address, bool cube) {"
CPP_NEW = """// Look up what the game *actually resolved* when its front-buffer object's
// fetch pointer could not be matched. Never return depth/shadow/cube targets.
// The frame age prevents presenting unrelated stale gameplay images.
ResolvedTexture* ResolvedTextures::FindNewestPresentable(
    uint32_t wanted_width, uint32_t wanted_height, uint64_t max_frame_age) {
  ApplyInvalidations();
  ResolvedTexture* best = nullptr;
  bool best_exact_size = false;
  for (auto& [key, item] : textures_) {
    if (!item.texture || !item.srv || item.invalidated || item.shadow ||
        item.cube || !item.last_resolve || item.guest_width < 320 ||
        item.guest_height < 180) continue;
    if (frame_ >= item.last_resolve_frame &&
        frame_ - item.last_resolve_frame > max_frame_age) continue;
    const bool exact_size = item.guest_width == wanted_width &&
                            item.guest_height == wanted_height;
    if (!best || (exact_size && !best_exact_size) ||
        (exact_size == best_exact_size &&
         item.last_resolve > best->last_resolve)) {
      best = &item;
      best_exact_size = exact_size;
    }
  }
  return best;
}

""" + CPP_ANCHOR
R_CVAR = 'REXCVAR_DEFINE_BOOL(native_x_renderer, false, "NativeXRenderer",'
R_CVAR_NEW = """// Hulk experiment: recover a non-black host picture from a *captured color
// Resolve* when the Xbox 360 front-buffer object fetch address is untracked.
// Off is always available for exact/native debugging.
REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_front_fallback, true, "NativeXRenderer",
                    "Hulk fallback to latest valid output-size guest color resolve if front fetch misses");
""" + R_CVAR
R_LOOKUP = """  if (c.has_texture) {
    fetch1 = c.fetch[1];
    front = g_resolved.FindNewest(GpuAddress(fetch1 & 0xFFFFF000u));
    if (front) g_resolved.MarkUsed(front);
  }
  const auto& desc = g_device->swap_chain()->GetDesc();"""
R_LOOKUP_NEW = """  if (c.has_texture) {
    fetch1 = c.fetch[1];
    front = g_resolved.FindNewest(GpuAddress(fetch1 & 0xFFFFF000u));
    if (front) g_resolved.MarkUsed(front);
  }
  // The Hulk XDK 6534 front object's fetch shadow may not be the same
  // guest address as the most recently resolved full-screen color.
  // Do not replace valid direct-front resolutions.
  bool hulk_front_fallback = false;
  if (!front && REXCVAR_GET(native_x_renderer_hulk_front_fallback)) {
    front = g_resolved.FindNewestPresentable(
        g_profile->output_width, g_profile->output_height, 600);
    if (front) {
      hulk_front_fallback = true;
      g_resolved.MarkUsed(front);
    }
  }
  const auto& desc = g_device->swap_chain()->GetDesc();"""
R_LOG = """    REXLOG_INFO("[HULK/NATIVEX] present {} guest_front={:#x} has_fetch={} fetch1={:#x} "
                "resolved={} size={}x{}",
                hulk_present_samples, g_front_buffer, c.has_texture, fetch1,
                front != nullptr && front->srv != nullptr,
                front ? front->width : 0, front ? front->height : 0);"""
R_LOG_NEW = """    REXLOG_INFO("[HULK/NATIVEX] present {} guest_front={:#x} has_fetch={} fetch1={:#x} "
                "resolved={} fallback={} selected={:#x} guest_size={}x{} host_size={}x{}",
                hulk_present_samples, g_front_buffer, c.has_texture, fetch1,
                front != nullptr && front->srv != nullptr, hulk_front_fallback,
                front ? front->address : 0,
                front ? front->guest_width : 0, front ? front->guest_height : 0,
                front ? front->width : 0, front ? front->height : 0);"""
R_COMPOSE = """  ComposePicture(front ? front->srv : nullptr, front ? front->width : 0, front ? front->height : 0, fetch1 & 0x3F,
                 (c.fetch[3] >> 1) & 0xFFF, back_buffer);"""
R_COMPOSE_NEW = """  // A recovered resolve has its own format and no valid front-fetch swizzle:
  // use its actual guest format and an identity channel mapping.
  const uint32_t present_format = hulk_front_fallback && front
      ? front->guest_format : (fetch1 & 0x3F);
  const uint32_t present_swizzle = hulk_front_fallback
      ? Blitter::Options{}.swizzle : ((c.fetch[3] >> 1) & 0xFFF);
  ComposePicture(front ? front->srv : nullptr, front ? front->width : 0,
                 front ? front->height : 0, present_format,
                 present_swizzle, back_buffer);"""


def replace_exact(src: str, old: str, new: str) -> tuple[str, bool]:
    if src.count(new) == 1:
        return src, False
    if src.count(old) != 1:
        raise ValueError("NativeX upstream Hulk fallback anchor mismatch")
    return src.replace(old, new), True


def patch_targets_header(source: str) -> tuple[str, bool]:
    return replace_exact(source, H_ANCHOR, H_NEW)


def patch_targets_cpp(source: str) -> tuple[str, bool]:
    return replace_exact(source, CPP_ANCHOR, CPP_NEW)


def patch_renderer(source: str) -> tuple[str, bool]:
    if source.count('  if (call == Call::kPresent) g_front_buffer = ctx.r4.u32;') != 1:
        raise ValueError("Missing Hulk logical Swap front setter")
    changes = []
    for old, new in ((R_CVAR, R_CVAR_NEW), (R_LOOKUP, R_LOOKUP_NEW),
                     (R_LOG, R_LOG_NEW), (R_COMPOSE, R_COMPOSE_NEW)):
        source, changed = replace_exact(source, old, new)
        changes.append(changed)
    if any(changes) and not all(changes):
        raise ValueError("Partial Hulk fallback renderer patch detected")
    return source, any(changes)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_front_fallback.py <NativeXRenderer checkout>")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN:
        raise ValueError("NativeXRenderer upstream SHA changed; refuse unsafe patch")
    pairs = (
        ("src/targets.h", patch_targets_header),
        ("src/targets.cpp", patch_targets_cpp),
        ("src/renderer.cpp", patch_renderer),
    )
    updates = []
    for relpath, transform in pairs:
        path = root / relpath
        output, changed = transform(path.read_text(encoding="utf-8"))
        updates.append((path, output, changed))
    if len({changed for _, _, changed in updates}) > 1:
        raise ValueError("Partial Hulk front fallback patch; refusing mixed source")
    for path, output, changed in updates:
        if changed:
            path.write_text(output, encoding="utf-8", newline="\n")
    print("Hulk NativeX 6534 front Resolve fallback:", "installed" if updates[0][2] else "already patched")


if __name__ == "__main__":
    main()
