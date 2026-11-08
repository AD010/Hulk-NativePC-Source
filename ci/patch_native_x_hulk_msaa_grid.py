#!/usr/bin/env python3
"""Hulk Vulkan RT sample-grid expansion for the observed 640x360 x4 -> 1280x720 output.

The Xbox 360 scene's 4x MSAA surface covers 640x360 *four-sample* pixels,
while the output resolve is 1280x720. Generic NativeXRenderer discards
guest MSAA and allocates an ordinary 640x360 target. That risks drawing
only a quarter of the full 1280x720 texture after native 2x upscale.

For exactly the half-output, four-sample surface of this Hulk profile,
interpret the two-by-two guest sample grid as full-resolution single-sample
native pixels. Do NOT instantiate Xenos/EDRAM or touch other surfaces.
The transformation is gated by an opt-in config variable, and logged.
"""
from pathlib import Path
import subprocess
import sys

PIN = "bf965e952c98e050753e91d08f32685a9f754b12"
PATCHES = {
 "src/renderer.cpp": [
  (
    "namespace native_x_renderer::renderer {\nnamespace {",
    "REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_expand_msaa_sample_grid, false, \"NativeXRenderer\",\n"
    "                    \"Hulk 2008: treat 640x360 4x MSAA scene as 1280x720 native sample-grid pixels,\"\n"
    "                    \" without the guest EDRAM; off unless set in Hulk TOML\");\n\n"
    "namespace native_x_renderer::renderer {\nnamespace {"
  ),
  (
    "  return index < 4 ? ReadSurface(base, Load32(base, device + d.render_targets + 4 * index), false, o)\n"
    "                   : ReadSurface(base, Load32(base, device + d.depth_stencil), true, o);",
    "  GuestSurface surface =\n"
    "      index < 4 ? ReadSurface(base, Load32(base, device + d.render_targets + 4 * index), false, o)\n"
    "                : ReadSurface(base, Load32(base, device + d.depth_stencil), true, o);\n"
    "  // Hulk's 640x360 4x surface has four pixel samples per nominal texel.\n"
    "  // This renderer deliberately does not emulate guest MSAA/EDRAM, so use\n"
    "  // that 2x2 pixel grid as a full-resolution native render target instead\n"
    "  // of rendering into a 640x360 single-sample texture (a quarter frame).\n"
    "  // Other games and Hulk's unrelated 4x targets remain untouched.\n"
    "  if (REXCVAR_GET(native_x_renderer_hulk_expand_msaa_sample_grid) && surface &&\n"
    "      surface.msaa() == 2 && g_profile->output_width % 2 == 0 &&\n"
    "      g_profile->output_height % 2 == 0 &&\n"
    "      surface.width == g_profile->output_width / 2 &&\n"
    "      surface.height == g_profile->output_height / 2) {\n"
    "    const uint32_t old_width = surface.width, old_height = surface.height;\n"
    "    surface.width *= 2;\n"
    "    surface.height *= 2;\n"
    "    // NativeX host targets have one sample. The encoded EDRAM pitch stays\n"
    "    // in the key to avoid aliasing the distinct native 1280x720 surface.\n"
    "    surface.surface_info &= ~(3u << 16);\n"
    "    static std::atomic<uint32_t> hulk_sample_grids{0};\n"
    "    if (hulk_sample_grids.fetch_add(1, std::memory_order_relaxed) < 8) {\n"
    "      REXLOG_INFO(\"[HULK/MSAA-GRID] {} surface {}x{} 4x samples -> {}x{} native pixels,\"\n"
    "                  \" EDRAM sample emulation off (pitch {})\",\n"
    "                  surface.depth ? \"depth\" : \"color\", old_width, old_height,\n"
    "                  surface.width, surface.height, surface.pitch());\n"
    "    }\n"
    "  }\n"
    "  return surface;"
  ),
  (
    "  if (!REXCVAR_GET(native_x_renderer_hulk_gap_single_draw)) return;\n"
    "  TrackedCall synthetic{};",
    "  if (!REXCVAR_GET(native_x_renderer_hulk_gap_single_draw)) return;\n"
    "  // Log 119: ~193 extra PM4 draws each frame were enqueued with no bound\n"
    "  // color or depth surface. They cannot write a Vulkan pixel. Retain the\n"
    "  // tracker's state record for the next real draw, but don't enqueue them.\n"
    "  bool has_surface = false;\n"
    "  for (uint32_t i = 0; i < 5; ++i) {\n"
    "    const GuestSurface s = ReadBoundSurface(base, device, i);\n"
    "    has_surface |= bool(s) && s.width && s.height;\n"
    "  }\n"
    "  if (!has_surface) {\n"
    "    static std::atomic<uint64_t> hulk_no_target_skipped{0};\n"
    "    const uint64_t n = ++hulk_no_target_skipped;\n"
    "    if (n == 1 || n % 10000 == 0) {\n"
    "      REXLOG_INFO(\"[HULK/PM4-GUARD] skipped {} non-presenting packet draws\"\n"
    "                  \" with no color/depth render target\", n);\n"
    "    }\n"
    "    return;\n"
    "  }\n"
    "  TrackedCall synthetic{};"
  ),
 ],
}

def transform(source, rules):
    flags = [new in source for old, new in rules]
    if all(flags):
        return source, False
    if any(flags):
        raise ValueError("Partial Hulk native MSAA grid patch")
    for old, new in rules:
        if source.count(old) != 1:
            raise ValueError("Pinned renderer anchor mismatch")
    for old, new in rules:
        source = source.replace(old, new)
    return source, True

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_msaa_grid.py <pinned NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if revision != PIN:
        raise ValueError("NativeXRenderer git pin mismatch " + revision)
    file = root / "src/renderer.cpp"
    after, changed = transform(file.read_text(encoding="utf-8"), PATCHES["src/renderer.cpp"])
    if changed:
        file.write_text(after, encoding="utf-8", newline="\n")
    print("Hulk 4x MSAA native sample grid:", "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
