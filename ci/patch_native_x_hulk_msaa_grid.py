#!/usr/bin/env python3
"""Opt-in Hulk Vulkan sample-grid CVar and non-rendering PM4 draw guard.

The guest surface and its encoded MSAA/pitch must stay intact. This script
only defines the experiment's config switch and filters PM4 calls with no
color or depth render target. A separate pinned patch adjusts host target
scaling and resolve rectangles without rewriting guest surface metadata.
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
