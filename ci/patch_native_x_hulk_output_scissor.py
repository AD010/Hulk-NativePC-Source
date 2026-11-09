#!/usr/bin/env python3
"""Hulk 2008: repair the narrow output scissor, audit actual MSAA resolves.

Log 124 proves the output render target is 1280x720, while the native draw
sets a guest scissor of [0, 640] with an intentionally huge 8192-wide viewport.
At 2x native scaling only the left 1280 of 2560 host pixels can be written.
This bounded repair widens ONLY the exact half-output scissor in full-surface
passes, leaving genuine smaller scissors, EDRAM keys and the 640x360 scene's
MSAA target alone.

In addition, observe the real 4x MSAA -> guest resolve destination *after*
load, instead of only the first 24 boot-time resolves.
"""
from pathlib import Path
import subprocess
import sys

import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
PATCHES = {
  "src/draw.cpp": [
    (
      'REXCVAR_DEFINE_INT32(native_x_renderer_draw_log, 0, "NativeXRenderer",',
      'REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_full_output_scissor, false, "NativeXRenderer",\n'
      '                    "Hulk: widen exact half-sized legacy scissor on full-output postprocess draws");\n'
      'REXCVAR_DEFINE_INT32(native_x_renderer_draw_log, 0, "NativeXRenderer",'
    ),
    (
      '  // Set when they change (D81): DiligentCore records them on every call.',
      '  // Hulk 2008: the command stream sometimes carries a legacy 640-wide\n'
      '  // scissor into the 1280-wide single-sampled output target. The viewport\n'
      '  // is unbounded (8192), so this is a full-output draw, not a UI subrect.\n'
      '  // Fix ONLY this exact mismatch. 640x360/4x scene targets, true partial\n'
      '  // scissors, and targets in other profiles remain completely unchanged.\n'
      '  if (REXCVAR_GET(native_x_renderer_hulk_full_output_scissor) && !sized->depth &&\n'
      '      sized->guest_width == 1280 && sized->guest_height == 720 &&\n'
      '      scissor.offset[0] == 0 && scissor.offset[1] == 0 &&\n'
      '      viewport_info.xy_extent[0] >= 1280 && viewport_info.xy_extent[1] >= 720) {\n'
      '    const bool legacy_half_x = scissor.extent[0] == 640 && scissor.extent[1] >= 720;\n'
      '    const bool legacy_half_xy = scissor.extent[0] == 640 && scissor.extent[1] == 360;\n'
      '    if (legacy_half_x || legacy_half_xy) {\n'
      '      const int32_t old_right = scissor_rect.right;\n'
      '      scissor_rect.right = int32_t(sized->width);\n'
      '      if (legacy_half_xy) scissor_rect.bottom = int32_t(sized->height);\n'
      '      static std::atomic<uint32_t> expanded{0};\n'
      '      const uint32_t n = expanded.fetch_add(1, std::memory_order_relaxed) + 1;\n'
      '      if (n <= 12 || n % 10000 == 0)\n'
      '        REXLOG_INFO("[HULK/SCISSOR-FIX] pass#{} surface={}x{} guestScissor={}x{} "\n'
      '                    "hostRight {}->{} hostBottom={} viewport={}x{}", n,\n'
      '                    sized->guest_width, sized->guest_height,\n'
      '                    scissor.extent[0], scissor.extent[1], old_right, scissor_rect.right,\n'
      '                    scissor_rect.bottom, viewport_info.xy_extent[0], viewport_info.xy_extent[1]);\n'
      '    }\n'
      '  }\n'
      '  // Set when they change (D81): DiligentCore records them on every call.'
    )
  ],
  "src/renderer.cpp": [
    (
      '  ITextureView* target_view = target ? target->SliceView(slice) : nullptr;',
      '  // The first 24 frame-path resolves happen before the scene loads.\n'
      '  // Observe the real 640x360 4x scene source and its destination.\n'
      '  if (surface && surface.msaa() == 2 && surface.width == 640 &&\n'
      '      surface.height == 360 && source) {\n'
      '    static std::atomic<uint32_t> scene_resolves{0};\n'
      '    const uint32_t n = scene_resolves.fetch_add(1, std::memory_order_relaxed) + 1;\n'
      '    if (n <= 16 || n % 20000 == 0)\n'
      '      REXLOG_INFO("[HULK/SCENE-RESOLVE] #{} src guest={}x{} {}xMSAA "\n'
      '                  "src host={}x{} dst guest={}x{} dst host={}x{} flags={:#x}",\n'
      '                  n, surface.width, surface.height, surface.samples(),\n'
      '                  source->width, source->height,\n'
      '                  target ? target->guest_width : 0, target ? target->guest_height : 0,\n'
      '                  target ? target->width : 0, target ? target->height : 0, flags);\n'
      '  }\n'
      '  ITextureView* target_view = target ? target->SliceView(slice) : nullptr;'
    )
  ]
}

def transform(source: str, rules):
    completed = [new in source for old, new in rules]
    if all(completed):
        return source, False
    if any(completed):
        raise ValueError("Partially applied Hulk output scissor / resolve audit patch")
    for old, new in rules:
        if source.count(old) != 1:
            raise ValueError("Pinned NativeXRenderer source anchor missing or duplicated")
    for old, new in rules:
        source = source.replace(old, new)
    return source, True

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_output_scissor.py <NativeXRenderer checkout>")
    root = Path(sys.argv[1]).resolve()
    revision = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != PIN:
        raise ValueError("Unexpected NativeXRenderer revision: " + revision)
    modified = []
    for rel, rules in PATCHES.items():
        file = root / rel
        source, changed = transform(file.read_text(encoding="utf-8"), rules)
        modified.append((file, source, changed))
    if len({changed for _, _, changed in modified}) != 1:
        raise ValueError("Mixed Hulk scissor patch states; no partial writes")
    for file, source, changed in modified:
        if changed:
            file.write_text(source, encoding="utf-8", newline="\n")
    print("Hulk native Vulkan bounded output scissor fix and live scene resolve audit:",
          "installed" if modified[0][2] else "already installed")

if __name__ == "__main__":
    main()
