#!/usr/bin/env python3
"""Real C++ source-contract test for Hulk's host-depth/stencil packing gate."""
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest
from patch_native_x_hulk_depth_stencil_grid import patch_text, OLD, NEW


class HulkStencilGridTests(unittest.TestCase):
    def test_source_idempotent_and_source_drift(self):
        before = "REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_expand_msaa_sample_grid, false);\n" + OLD
        result, changed = patch_text(before)
        self.assertTrue(changed)
        self.assertEqual(patch_text(result), (result, False))
        self.assertIn("HULK/DEPTH-STENCIL-GRID-PARITY", result)
        self.assertIn("(!scaled || hulk_depth_stencil_exact_pixels)", result)
        self.assertIn("!aliased", result)
        self.assertIn("source->samples == 1", result)
        self.assertIn("from.width == to.width", result)
        self.assertNotIn(OLD, result)
        for malformed in ("unreviewed", OLD, "HULK/DEPTH-STENCIL-GRID-PARITY"):
            with self.assertRaises(ValueError):
                patch_text(malformed)

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),
                         "A C++ compiler is required")
    def test_real_cxx_gate_with_actual_patch_body(self):
        pre = r"""
#include <cstdint>
#include <cassert>
struct Surface {
 bool depth = true;
 int sample_mode = 2;
 int width = 640, height = 360;
 explicit operator bool() const { return width && height; }
 int msaa() const { return sample_mode; }
};
struct Rect { int x=0,y=0; uint32_t width=2560,height=1440; };
struct Target {
 bool depth = true;
 bool reduced = false;
 bool stencil_srv = true;
 bool read_packed = true;
 int samples = 1;
};
bool ShouldPack(bool grid, bool scaled, bool aliased, bool is_depth,
                bool stencil, bool matching_rect, bool multisample) {
 Surface surface; surface.depth = is_depth;
 Target src; src.depth = is_depth; src.stencil_srv = stencil;
 Target dst; dst.read_packed = true;
 src.samples = multisample ? 4 : 1;
 Target* source = &src;
 Target* target = &dst;
 Rect from, to;
 if (!matching_rect) to.width = 1280;
 bool cube = false;
 bool grid_enabled = grid;
#define REXCVAR_GET(variable) grid_enabled
"""
        post = r"""
    return true;
  }
  return false;
}
int main() {
  assert(ShouldPack(true, true, false, true, true, true, false));
  assert(!ShouldPack(false, true, false, true, true, true, false));
  assert(!ShouldPack(true, true, true, true, true, true, false));
  assert(!ShouldPack(true, true, false, false, true, true, false));
  assert(!ShouldPack(true, true, false, true, false, true, false));
  assert(!ShouldPack(true, true, false, true, true, false, false));
  assert(!ShouldPack(true, true, false, true, true, true, true));
  assert(ShouldPack(false, false, false, true, true, true, false));
}
"""
        cpp = pre + NEW + post
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "stencil.cpp"
            executable = Path(directory) / ("stencil.exe" if os.name == "nt"
                                            else "stencil")
            source.write_text(cpp, encoding="utf-8")
            compiler = shutil.which("clang++") or shutil.which("g++")
            subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra",
                            "-Werror", str(source), "-o", str(executable)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(executable)], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
