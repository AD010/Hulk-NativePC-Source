#!/usr/bin/env python3
"""Hulk NativeX: scaled copy when the native 4x-MSAA scene is resolved.

Hulk's 640x360 guest 4x-MSAA scene is rendered into 2560x1440 native pixels
with the opt-in sample-grid target. The destination may still be a 640x360
guest texture at standard 2x host scale (1280x720). Previously the Resolve
detected a scale mismatch only for reduced views, aliases or a *1280x720*
destination. It silently truncated copies to the smaller of the host
rectangles for intermediate/postprocess targets. This patch handles the
actual source/dest scale mismatch, preserving original guest descriptors.
"""
from pathlib import Path
import subprocess
import sys

PIN = "bf965e952c98e050753e91d08f32685a9f754b12"
PATCHES = {
 "src/renderer.cpp": [
   (
     "  bool scaled = source->reduced || aliased || hulk_sample_grid;",
     "  // The Hulk native 4x-MSAA scene uses double the *host* scale of all\n"
     "  // normal targets, even when the guest source and destination are both\n"
     "  // 640x360. Treat this as a filtered copy, not a same-pixel-size copy:\n"
     "  // otherwise min(src,dst) clips the right/bottom halves of the scene.\n"
     "  const bool hulk_host_scale_mismatch =\n"
     "      REXCVAR_GET(native_x_renderer_hulk_expand_msaa_sample_grid) &&\n"
     "      surface && surface.msaa() == 2 &&\n"
     "      surface.width * 2 == g_profile->output_width &&\n"
     "      surface.height * 2 == g_profile->output_height &&\n"
     "      (source->scale.x != target->scale.x || source->scale.y != target->scale.y);\n"
     "  bool scaled = source->reduced || aliased || hulk_sample_grid || hulk_host_scale_mismatch;\n"
     "  if (hulk_host_scale_mismatch) {\n"
     "    static std::atomic<uint64_t> hulk_copies{0};\n"
     "    const uint64_t n = ++hulk_copies;\n"
     "    if (n <= 16 || n % 10000 == 0)\n"
     "      REXLOG_INFO(\"[HULK/RESOLVE-SCALE] #{} guest {}x{} 4xMSAA -> {}x{};\"\n"
     "                  \" host-scale {:.2f},{:.2f} -> {:.2f},{:.2f}\", n,\n"
     "                  surface.width, surface.height, target->guest_width,\n"
     "                  target->guest_height, source->scale.x, source->scale.y,\n"
     "                  target->scale.x, target->scale.y);\n"
     "  }"
   )
 ]
}


# Log the actual guest Resolve return addresses to identify pre-HUD scene edge.
# The HUD and front buffer must NOT be antialiased speculatively.
PATCHES["src/renderer.cpp"].append((
 "  ITextureView* target_view = target ? target->SliceView(slice) : nullptr;",
 "  if (surface && source && target && !surface.depth &&\n"
 "      ((surface.msaa() == 2 && surface.width == 640 && surface.height == 360) ||\n"
 "       (target->guest_width == g_profile->output_width &&\n"
 "        target->guest_height == g_profile->output_height))) {\n"
 "    const bool scene4x = surface.msaa() == 2 && surface.width == 640 && surface.height == 360;\n"
 "    static std::atomic<uint32_t> scene_seen{0}, output_seen{0};\n"
 "    const uint32_t count = (scene4x ? scene_seen : output_seen).fetch_add(1, std::memory_order_relaxed) + 1;\n"
 "    if (count <= 16 || count % 10000 == 0)\n"
 "      REXLOG_INFO(\"[HULK/RESOLVE-CALLER] kind={} #{} LR={:#x} src={}x{} {}xMSAA\"\n"
 "                  \" host={}x{} dst={}x{} host={}x{} flags={:#x}\",\n"
 "                  scene4x ? \"scene4x\" : \"output\", count, c.caller,\n"
 "                  surface.width, surface.height, surface.samples(),\n"
 "                  source->width, source->height, target->guest_width, target->guest_height,\n"
 "                  target->width, target->height, flags);\n"
 "  }\n"
 "  ITextureView* target_view = target ? target->SliceView(slice) : nullptr;"
))

def transform(source, rules):
    marks = [new in source for old, new in rules]
    if all(marks):
        return source, False
    if any(marks):
        raise ValueError("Partial host-scale Resolve patch")
    for old, new in rules:
        if source.count(old) != 1:
            raise ValueError("Hulk Resolve anchor drift / repeated")
        source = source.replace(old, new)
    return source, True

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_resolve_scale.py <NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if sha != PIN:
        raise ValueError("Pinned NativeX version mismatch " + sha)
    dst = root / "src/renderer.cpp"
    after, changed = transform(dst.read_text(encoding="utf-8"),PATCHES["src/renderer.cpp"])
    if changed:
        dst.write_text(after,encoding="utf-8",newline="\n")
    print("Hulk correct native 4x-MSAA intermediate target Resolve scaling:",
          "patched" if changed else "already installed")

if __name__ == "__main__":
    main()
