#!/usr/bin/env python3
"""Smoke tests for bounded Hulk output-scissor and live MSAA resolve audit."""
import unittest
import shutil
import subprocess
import tempfile
from pathlib import Path
from patch_native_x_hulk_output_scissor import PATCHES, transform

class HulkOutputScissorTests(unittest.TestCase):
    def test_pinned_patch_idempotence_and_fail_closed(self):
        for path, rules in PATCHES.items():
            with self.subTest(path=path):
                original = "\n".join(old for old, new in rules)
                updated, changed = transform(original, rules)
                self.assertTrue(changed)
                self.assertEqual(transform(updated, rules), (updated, False))
                with self.assertRaises(ValueError):
                    transform("unrelated source", rules)
                if len(rules) > 1:
                    partially_patched = rules[0][1] + "\n" + "\n".join(old for old, new in rules[1:])
                    with self.assertRaises(ValueError):
                        transform(partially_patched, rules)

    def test_exact_half_width_output_only(self):
        patch = "\n".join(new for old, new in PATCHES["src/draw.cpp"])
        self.assertIn("native_x_renderer_hulk_full_output_scissor, false", patch)
        self.assertIn("sized->guest_width == 1280", patch)
        self.assertIn("sized->guest_height == 720", patch)
        self.assertIn("scissor.extent[0] == 640", patch)
        self.assertIn("viewport_info.xy_extent[0] >= 1280", patch)
        self.assertIn("!sized->depth", patch)
        self.assertIn("scissor_rect.right = int32_t(sized->width)", patch)
        self.assertIn("[HULK/SCISSOR-FIX]", patch)
        self.assertNotIn("viewport.Width =", patch)

    def test_scissor_clips_to_real_framebuffer_without_touching_viewport(self):
        cpp = "\n".join(new for old, new in PATCHES["src/draw.cpp"])
        for required in (
            "HULK/SCISSOR-FRAMEBUFFER-CLIP",
            "scissor_rect.left = std::clamp(",
            "scissor_rect.top = std::clamp(",
            "scissor_rect.right = std::clamp(",
            "scissor_rect.bottom = std::clamp(",
            "int32_t(sized->width)", "int32_t(sized->height)"
        ):
            self.assertIn(required, cpp)
        self.assertNotIn("viewport.Width =", cpp)
        self.assertNotIn("viewport.Height =", cpp)

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),
                         "C++ compiler unavailable")
    def test_native_scissor_intersection_cpp(self):
        # Compile the SAME four bound assignments embedded in the patch.
        output = "\n".join(new for old, new in PATCHES["src/draw.cpp"])
        first = output.index("  scissor_rect.left = std::clamp(")
        last = output.index("  if (scissor_rect != hulk_unclipped_scissor)", first)
        body = output[first:last]
        code = (
            "#include <algorithm>\n#include <cstdint>\n#include <cassert>\n"
            "struct Rect { int32_t left, top, right, bottom; };\n"
            "Rect clip(Rect scissor_rect, int32_t width, int32_t height) {\n"
            " const int32_t hulk_framebuffer_w = width;\n"
            " const int32_t hulk_framebuffer_h = height;\n"
            + body + " return scissor_rect;\n}\n"
            "int main() {\n"
            " auto a=clip({0,0,2560,32768},2560,1440);\n"
            " assert(a.left==0&&a.top==0&&a.right==2560&&a.bottom==1440);\n"
            " auto b=clip({200,300,800,600},2560,1440);\n"
            " assert(b.left==200&&b.top==300&&b.right==800&&b.bottom==600);\n"
            " auto c=clip({-100,-200,400,200},2560,1440);\n"
            " assert(c.left==0&&c.top==0&&c.right==400&&c.bottom==200);\n"
            " auto d=clip({5000,2000,32768,32768},2560,1440);\n"
            " assert(d.left==2560&&d.top==1440&&d.right==2560&&d.bottom==1440);\n"
            "}\n"
        )
        compiler = shutil.which("clang++") or shutil.which("g++")
        with tempfile.TemporaryDirectory(prefix="hulk_scissor_") as tmp:
            path = Path(tmp)
            source = path / "scissor.cpp"
            exe = path / "scissor"
            source.write_text(code, encoding="utf-8")
            subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra",
                            "-Werror", str(source), "-o", str(exe)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(exe)], check=True, capture_output=True)

    def test_full_msaa_scene_resolve_audit(self):
        patch = "\n".join(new for old, new in PATCHES["src/renderer.cpp"])
        self.assertIn("surface.msaa() == 2", patch)
        self.assertIn("surface.width == 640", patch)
        self.assertIn("surface.height == 360", patch)
        self.assertIn("target ? target->guest_width : 0", patch)
        self.assertIn("[HULK/SCENE-RESOLVE]", patch)

if __name__ == "__main__":
    unittest.main()
