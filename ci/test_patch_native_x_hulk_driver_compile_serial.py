#!/usr/bin/env python3
"""Synthetic Hulk driver serialization regressions, with no game assets."""
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest
from patch_native_x_hulk_driver_compile_serial import (
    patch, HEADER, SHADER_CALL, PIPELINE_CALL, CVAR, SHADER_LOCK, PIPELINE_LOCK)

def source_fixture():
    shaders = ('#include "shaders.h"\n#include <rex/logging.h>\n'
               'namespace native_x_renderer {\n'
               '// HULK/NVIDIA-COMPILE-BOUNDARY\n'
               + SHADER_CALL + '\n' + SHADER_CALL + '\n}')
    pipelines = ('#include "pipelines.h"\n#include <xxhash.h>\n'
                 'namespace native_x_renderer {\n'
                 '// HULK/NVIDIA-COMPILE-BOUNDARY\n'
                 + PIPELINE_CALL + '\n}')
    return shaders, pipelines

class VulkanCreateGateTests(unittest.TestCase):
    def test_install_and_repeat(self):
        a, b = source_fixture()
        sh, pl, changed = patch(a, b)
        self.assertTrue(changed)
        self.assertEqual(patch(sh, pl), (sh, pl, False))
        self.assertEqual(sh.count(SHADER_LOCK), 2)
        self.assertEqual(pl.count(PIPELINE_LOCK), 1)
        self.assertEqual(sh.count(CVAR), 1)
        self.assertEqual(sh.count("HulkDriverCompileMutex()"), 2)
        self.assertEqual(pl.count("HulkDriverCompileMutex()"), 1)
        self.assertEqual(sh.count(SHADER_CALL), 2)
        self.assertEqual(pl.count(PIPELINE_CALL), 1)
        self.assertIn("std::defer_lock", sh)
        self.assertIn("std::defer_lock", pl)
        self.assertIn("REXCVAR_DECLARE(bool, native_x_renderer_hulk_serial_driver_creation);", pl)

    def test_reject_source_drift(self):
        a, b = source_fixture()
        with self.assertRaises(ValueError):
            patch(a.replace(SHADER_CALL, "other()", 1), b)
        with self.assertRaises(ValueError):
            patch(a, b.replace(PIPELINE_CALL, "broken()"))
        changed_a, _, _ = patch(a, b)
        with self.assertRaises(ValueError):
            patch(changed_a, b)

    def test_inline_header_one_gate_shared(self):
        self.assertIn("inline std::mutex& HulkDriverCompileMutex()", HEADER)
        self.assertIn("static std::mutex mutex;", HEADER)
        compiler = shutil.which("c++")
        if not compiler:
            self.skipTest("no standalone C++ compiler")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "hulk_compiler_gate.h").write_text(HEADER)
            (root / "a.cpp").write_text(
                '#include "hulk_compiler_gate.h"\n'
                'std::mutex* a() { return &native_x_renderer::HulkDriverCompileMutex(); }\n')
            (root / "main.cpp").write_text(
                '#include "hulk_compiler_gate.h"\n'
                'extern std::mutex* a();\n'
                'int main() { return a() != &native_x_renderer::HulkDriverCompileMutex(); }\n')
            exe = root / "check"
            subprocess.run([compiler, "-std=c++17", "-pthread",
                str(root / "a.cpp"), str(root / "main.cpp"), "-o", str(exe)],
                check=True)
            subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    unittest.main()
