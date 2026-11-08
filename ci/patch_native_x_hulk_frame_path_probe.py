#!/usr/bin/env python3
"""Read-only NativeX Vulkan Hulk scene/output geometry probe.

Hulk logs 115/116: the 1280x720 guest output is allocated at 2560x1440
(scale=2), yet screenshots show only a top-left partial scene and sometimes
a lime-green remainder. Distinguish a wrong draw viewport from a truncated
MSAA resolve and a bad present source BEFORE altering the final blit.

Run on the pinned upstream after the existing NativeX patches.
"""
from pathlib import Path
import subprocess
import sys

PIN = "bf965e952c98e050753e91d08f32685a9f754b12"
PATCHES = {
    "src/draw.cpp": [
        ("#include <algorithm>\n#include <bit>",
         "#include <algorithm>\n#include <atomic>\n#include <bit>"),
        (
          "  Rect scissor_rect{host_x(scissor.offset[0]), host_y(scissor.offset[1]),\n"
          "                    host_x(int64_t(scissor.offset[0]) + scissor.extent[0]),\n"
          "                    host_y(int64_t(scissor.offset[1]) + scissor.extent[1])};",
          "  Rect scissor_rect{host_x(scissor.offset[0]), host_y(scissor.offset[1]),\n"
          "                    host_x(int64_t(scissor.offset[0]) + scissor.extent[0]),\n"
          "                    host_y(int64_t(scissor.offset[1]) + scissor.extent[1])};\n"
          "  // Diagnostic: identify whether the guest's fullscreen pass is\n"
          "  // incorrectly using a half-sized viewport inside a 2x host target.\n"
          "  static std::atomic<uint32_t> hulk_draw_rects{0};\n"
          "  const uint32_t hulk_rect = hulk_draw_rects.fetch_add(1, std::memory_order_relaxed);\n"
          "  if (hulk_rect < 40) REXLOG_INFO(\n"
          "      \"[HULK/FRAMEPATH] draw#{} surface guest={}x{} host={}x{} scale={:.3f},{:.3f} \"\n"
          "      \"viewport guest=({},{} {}x{}) host=({:.1f},{:.1f} {:.1f}x{:.1f}) \"\n"
          "      \"scissor guest=({},{} {}x{}) host=({},{},{},{})\",\n"
          "      hulk_rect, sized->guest_width, sized->guest_height, sized->width, sized->height,\n"
          "      scale_x, scale_y, viewport_info.xy_offset[0], viewport_info.xy_offset[1],\n"
          "      viewport_info.xy_extent[0], viewport_info.xy_extent[1],\n"
          "      viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height,\n"
          "      scissor.offset[0], scissor.offset[1], scissor.extent[0], scissor.extent[1],\n"
          "      scissor_rect.left, scissor_rect.top, scissor_rect.right, scissor_rect.bottom);"
        ),
    ],
    "src/renderer.cpp": [
        (
          "  to.width = from.width;\n  to.height = from.height;\n  copied = from;",
          "  to.width = from.width;\n  to.height = from.height;\n  copied = from;\n"
          "  static std::atomic<uint32_t> hulk_resolve_guest_probe{0};\n"
          "  if (target->guest_width == g_profile->output_width &&\n"
          "      target->guest_height == g_profile->output_height &&\n"
          "      hulk_resolve_guest_probe.fetch_add(1, std::memory_order_relaxed) < 24) {\n"
          "    REXLOG_INFO(\"[HULK/FRAMEPATH] resolve guest src={}x{} samples={} dst={}x{} \"\n"
          "                \"from=({},{} {}x{}) to=({},{} {}x{}) rect={} point={} flags={:#x}\",\n"
          "                surface.width, surface.height, surface.samples(),\n"
          "                target->guest_width, target->guest_height,\n"
          "                from.x, from.y, from.width, from.height,\n"
          "                to.x, to.y, to.width, to.height,\n"
          "                c.has_source_rect, c.has_dest_point, flags);\n"
          "  }"
        ),
        (
          "  if (!scaled) {\n    from.width = to.width = std::min(from.width, to.width);\n    from.height = to.height = std::min(from.height, to.height);\n  }",
          "  if (!scaled) {\n    from.width = to.width = std::min(from.width, to.width);\n    from.height = to.height = std::min(from.height, to.height);\n  }\n"
          "  static std::atomic<uint32_t> hulk_resolve_host_probe{0};\n"
          "  if (target->guest_width == g_profile->output_width &&\n"
          "      target->guest_height == g_profile->output_height &&\n"
          "      hulk_resolve_host_probe.fetch_add(1, std::memory_order_relaxed) < 24) {\n"
          "    REXLOG_INFO(\"[HULK/FRAMEPATH] resolve host src={}x{} dst={}x{} \"\n"
          "                \"from=({},{} {}x{}) to=({},{} {}x{}) aliased={} scaled={}\",\n"
          "                source->width, source->height, target->width, target->height,\n"
          "                from.x, from.y, from.width, from.height,\n"
          "                to.x, to.y, to.width, to.height, aliased, scaled);\n"
          "  }"
        ),
        (
          "  Blitter::Rect to{int32_t((desc.Width - scaled_width) / 2), int32_t((desc.Height - scaled_height) / 2), scaled_width,\n"
          "                   scaled_height};",
          "  Blitter::Rect to{int32_t((desc.Width - scaled_width) / 2), int32_t((desc.Height - scaled_height) / 2), scaled_width,\n"
          "                   scaled_height};\n"
          "  static std::atomic<uint32_t> hulk_present_probe{0};\n"
          "  if (hulk_present_probe.fetch_add(1, std::memory_order_relaxed) < 8) {\n"
          "    REXLOG_INFO(\"[HULK/FRAMEPATH] present source={}x{} swapchain={}x{} \"\n"
          "                \"blit=({},{} {}x{}) srv={} gamma={}\",\n"
          "                width, height, desc.Width, desc.Height, to.x, to.y, to.width, to.height,\n"
          "                srv != nullptr, gamma_applied);\n"
          "  }"
        )
    ],
}

def transform(source, rules):
    done = [new in source for old, new in rules]
    if all(done):
        return source, False
    if any(done):
        raise ValueError("Partially installed Hulk frame geometry audit")
    for old, new in rules:
        if source.count(old) != 1:
            raise ValueError("Pinned NativeX frame probe anchor missing or ambiguous")
    for old, new in rules:
        source = source.replace(old, new)
    return source, True

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_frame_path_probe.py <NativeXRenderer checkout>")
    root = Path(sys.argv[1]).resolve()
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if revision != PIN:
        raise ValueError("Unexpected NativeXRenderer revision: " + revision)
    results = []
    for rel, rules in PATCHES.items():
        f = root / rel
        after, changed = transform(f.read_text(encoding="utf-8"), rules)
        results.append((f, after, changed))
    if len({change for _,_,change in results}) != 1:
        raise ValueError("Mixed frame-path audit state; no partial writes")
    for f, after, changed in results:
        if changed:
            f.write_text(after, encoding="utf-8", newline="\n")
    print("Hulk native Vulkan frame-path probe:",
          "installed" if results[0][2] else "already installed")

if __name__ == "__main__":
    main()
