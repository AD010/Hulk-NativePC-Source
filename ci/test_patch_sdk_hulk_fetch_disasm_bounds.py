#!/usr/bin/env python3
"""Fail-closed text-only tests for unsafe SDK texture filter disassembly."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from patch_sdk_hulk_fetch_disasm_bounds import OLD, NEW, REPLACEMENTS, patch_text

def fixture():
    return (
        "// DataFormat=unlisted(0xDEAD)\n"
        + OLD + "\n"
        + "\n".join(f'  out->AppendFormat("{{}}", {old});'
                    for old in REPLACEMENTS) + "\n"
    )

class FetchDisasmBoundsTests(unittest.TestCase):
    def test_all_six_filters_are_checked_and_patch_is_atomic(self):
        result, changed = patch_text(fixture())
        self.assertTrue(changed)
        self.assertEqual(patch_text(result), (result, False))
        self.assertIn("HULK/TEXTURE-FETCH-DISASM-BOUNDS", result)
        for old, new in REPLACEMENTS.items():
            self.assertNotIn(old, result)
            self.assertEqual(result.count(new), 1)
        for raw in ("original", fixture().replace(OLD, ""),
                    fixture().replace("DataFormat=unlisted", "DataFormat")):
            with self.assertRaises(ValueError):
                patch_text(raw)
        with self.assertRaises(ValueError):
            patch_text(result.replace(next(iter(REPLACEMENTS.values())), "broken"))

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),
                         "Standalone C++ compiler unavailable")
    def test_actual_cpp_guard_checks_valid_and_invalid_enum_values(self):
        # Compile the exact source fragment inserted into the real SDK, not
        # a Python approximation. Unsafe filter values must not crash.
        pre = r"""
#include <cassert>
#include <cstring>
namespace xenos {
enum class TextureFilter : int { kUseFetchConst=99 };
enum class AnisoFilter : int { kUseFetchConst=99 };
}
int main() {
  static const char* kTextureFilterNames[] = {
      "point", "linear", "basemap", "keep",
  };
"""
        post = r"""
  for (int i=-64; i<=64; ++i) {
    const char* texture = hulk_texture_filter_name(
        static_cast<xenos::TextureFilter>(i));
    const char* aniso = hulk_aniso_filter_name(
        static_cast<xenos::AnisoFilter>(i));
    assert(texture && aniso);
    if (i >= 0 && i < 4)
      assert(std::strcmp(texture, kTextureFilterNames[i]) == 0);
    else
      assert(std::strcmp(texture, "unknown") == 0);
    if (i >= 0 && i < 7)
      assert(std::strcmp(aniso, kAnisoFilterNames[i]) == 0);
    else
      assert(std::strcmp(aniso, "unknown") == 0);
  }
}
"""
        # NEW includes the original anisotropic names array and new lambdas.
        source = pre + NEW + post
        with tempfile.TemporaryDirectory() as d:
            cpp=Path(d)/"filters.cpp"
            exe=Path(d)/("filters.exe" if os.name=="nt" else "filters")
            cpp.write_text(source,encoding="utf-8")
            compiler=shutil.which("clang++") or shutil.which("g++")
            subprocess.run([compiler,"-std=c++20","-Wall","-Wextra",
                            "-Werror",str(cpp),"-o",str(exe)],
                           check=True,capture_output=True,text=True)
            subprocess.run([str(exe)],check=True,capture_output=True,text=True)

if __name__ == "__main__":
    unittest.main()
