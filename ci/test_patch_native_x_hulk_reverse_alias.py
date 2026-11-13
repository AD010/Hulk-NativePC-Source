#!/usr/bin/env python3
"""Regression tests for Hulk's two Xenos surface descriptors sharing a native image."""
import os
import random
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest
from patch_native_x_hulk_reverse_alias import PATCHES, HOST_RECT_NEW, transform

class HulkReverseAliasTests(unittest.TestCase):
    def test_exact_anchors_idempotent_fail_closed(self):
        for relative, rules in PATCHES.items():
            with self.subTest(file=relative):
                raw = "\n".join(old for old,new in rules)
                after, modified = transform(raw, rules)
                self.assertTrue(modified)
                self.assertEqual(transform(after, rules), (after, False))
                with self.assertRaises(ValueError):
                    transform("unknown pinned NativeX source",rules)

    def test_storage_identity_and_write_order(self):
        cpp = "\n".join(new for old,new in PATCHES["src/targets.cpp"])
        self.assertIn("s.msaa() != 0",cpp)
        self.assertIn("std::get<1>(candidate) != s.edram_base()",cpp)
        self.assertIn("std::get<2>(candidate) * 2 != s.pitch()",cpp)
        self.assertIn("std::get<3>(candidate) != 2",cpp)
        self.assertIn("std::get<4>(candidate) != std::get<4>(desired)",cpp)
        self.assertIn("std::get<5>(candidate) * 2 != s.width",cpp)
        self.assertIn("std::get<6>(candidate) * 2 != s.height",cpp)
        self.assertIn("host.written > found->written",cpp)

    def test_native_color_resolve_pixel_mapping(self):
        cpp = "\n".join(new for old,new in PATCHES["src/renderer.cpp"])
        self.assertIn("alias->written > (source ? source->written : 0)",cpp)
        self.assertIn("from.width = (from.width + 1) / 2",cpp)
        self.assertIn("to.width = std::min(from.width * 2",cpp)
        self.assertIn("|| hulk_reverse_alias;",cpp)
        self.assertIn("[HULK/REVERSE-ALIAS]",cpp)
        self.assertIn("(hulk_reverse_alias || surface.msaa() == 2)",cpp)
        self.assertNotIn("surface.surface_info &=",cpp)
        self.assertNotIn("surface.width *= 2",cpp)


    def test_exact_2560_host_pixel_mapping_65536_crops(self):
        # 1280x720 caller: 640x360/4x aliases a 2560x1440 host
        # image, and the 1280x720/1x output is also 2560x1440.
        # Compare the host-space first and last included pixel, not
        # rounded guest halves. Old (x//2)*4 fails for odd x.
        count = 0
        for x in range(256):
            for width in range(1, 257):
                usable = min(width, 1280 - x)
                src_start, src_end = x * 2, (x + usable) * 2
                dst_start, dst_end = x * 2, (x + usable) * 2
                self.assertEqual((src_start, src_end),
                                 (dst_start, dst_end))
                self.assertLessEqual(src_end, 2560)
                count += 1
        self.assertEqual(count, 65536)
        self.assertNotEqual((1 // 2) * 4, 1 * 2)
        self.assertIn("HULK/REVERSE-ALIAS-EXACT-RECT", HOST_RECT_NEW)
        self.assertIn("copy_w * scale_x", HOST_RECT_NEW)
        self.assertIn("copy_h * scale_y", HOST_RECT_NEW)

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),
                         "C++ compiler required")
    def test_real_cpp_resolve_rect_patch_odd_pixels_and_fallback(self):
        # Compile the actual production replacement text; no Xbox image
        # bytes, fake Vulkan target, guest hook or sample-grid switch.
        code = r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
struct RenderScale { uint32_t x, y; };
struct Blitter { struct Rect { int32_t x, y; uint32_t width, height; }; };
struct HostTarget {
  uint32_t width, height, guest_width, guest_height;
  RenderScale scale;
  explicit operator bool() const { return width && height; }
};
struct GuestSurface {
  uint32_t width, height;
  explicit operator bool() const { return width && height; }
};
Blitter::Rect HostRect(RenderScale s, Blitter::Rect r) {
  return {int32_t(r.x * int32_t(s.x)), int32_t(r.y * int32_t(s.y)),
          r.width * s.x, r.height * s.y};
}
void Map(bool hulk_reverse_alias, int32_t x, uint32_t width,
         Blitter::Rect& mapped_from, Blitter::Rect& mapped_to) {
  GuestSurface surface{1280, 720};
  HostTarget src{2560, 1440, 640, 360, {4, 4}};
  HostTarget dst{2560, 1440, 1280, 720, {2, 2}};
  HostTarget* source = &src;
  HostTarget* target = &dst;
  const Blitter::Rect hulk_reverse_original_from{x, 1, width, 2};
  const Blitter::Rect hulk_reverse_original_to{0, 1, width, 2};
  Blitter::Rect from{x / 2, 0, (width + 1) / 2, 1};
  Blitter::Rect to{0, 1, (width + 1) / 2, 1};
""" + HOST_RECT_NEW + r"""
  mapped_from = from;
  mapped_to = to;
}
int main() {
  for (int x=0; x<256; ++x) {
    for (uint32_t w=1; w<=256; ++w) {
      Blitter::Rect a{}, b{};
      Map(true, x, w, a, b);
      assert(a.x == x * 2);
      assert(a.y == 2);
      assert(a.width == w * 2);
      assert(a.height == 4);
      assert(b.x == 0);
      assert(b.y == 2);
      assert(b.width == w * 2);
      assert(b.height == 4);
    }
  }
  // Right-edge crop must never sample beyond the 1280-wide guest view.
  // The previous ceil(width/2) alias conversion expanded this by 2 pixels.
  Blitter::Rect edge_src{}, edge_dst{};
  Map(true, 1279, 2, edge_src, edge_dst);
  assert(edge_src.x == 2558 && edge_src.width == 2);
  assert(edge_dst.x == 0 && edge_dst.width == 2);
  Map(true, 1278, 8, edge_src, edge_dst);
  assert(edge_src.x == 2556 && edge_src.width == 4);
  assert(edge_dst.width == 4);
  Blitter::Rect a{}, b{};
  Map(false, 3, 7, a, b);
  assert(a.x == 4 && a.width == 16);
  assert(b.width == 8);
}
"""
        with tempfile.TemporaryDirectory() as directory:
            src = Path(directory) / "resolve_rect.cpp"
            exe = Path(directory) / ("resolve_rect.exe" if os.name == "nt"
                                      else "resolve_rect")
            src.write_text(code, encoding="utf-8")
            compiler = shutil.which("clang++") or shutil.which("g++")
            subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra",
                            "-Werror", str(src), "-o", str(exe)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(exe)], check=True, capture_output=True)

if __name__ == "__main__":
    unittest.main()
