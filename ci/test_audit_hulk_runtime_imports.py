#!/usr/bin/env python3
"""Synthetic tests for Hulk PE DLL dependencies, no copyrighted inputs."""
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from audit_hulk_runtime_imports import audit_directory, classify, parse_dependents


class RuntimeImportTests(unittest.TestCase):
    def test_dumpbin_normal_and_delayload(self):
        output = """Microsoft (R) COFF/PE Dumper
Image has the following dependencies:

    KERNEL32.dll
    VCRUNTIME140_1.dll
    rexruntime.dll

Image has the following delay load dependencies:

    vulkan-1.dll
    nvngx_dlss.dll
Summary
    .dll words in unrelated text
"""
        self.assertEqual(parse_dependents(output), sorted([
            "kernel32.dll", "vcruntime140_1.dll", "rexruntime.dll",
            "vulkan-1.dll", "nvngx_dlss.dll",
        ]))

    def test_classifies_local_system_vc_vulkan_and_missing(self):
        bundled = {"rexruntime.dll", "rexgpu-xenos.dll"}
        self.assertEqual(classify("ReXRuntime.DLL", bundled), "app_local")
        self.assertEqual(classify("KERNEL32.DLL", bundled), "windows_system")
        self.assertEqual(classify("api-ms-win-core-file-l1-2-0.dll", bundled), "windows_api_set")
        self.assertEqual(classify("vcruntime140.dll", bundled), "system_vc_redist")
        self.assertEqual(classify("vcruntime140_1.dll", bundled), "system_vc_redist")
        self.assertEqual(classify("msvcp140.dll", bundled), "system_vc_redist")
        self.assertEqual(classify("msvcp140_1.dll", bundled), "system_vc_redist")
        self.assertEqual(classify("vulkan-1.dll", bundled), "vulkan_loader_driver")
        self.assertEqual(classify("nvngx_dlss.dll", bundled), "unresolved_external")
        self.assertEqual(classify("libwinpthread-1.dll", bundled), "unresolved_external")

    def test_scans_all_shipping_binaries_not_only_exe(self):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            for name in ("incredible_hulk.exe", "rexruntime.dll", "rexgpu-xenos.dll"):
                (root / name).write_bytes(b"placeholder no copyrighted bytes")
            class MockResult:
                returncode = 0
                stderr = ""
                def __init__(self, output):
                    self.stdout = output
            def fake_dumpbin(argv, **kwargs):
                name = Path(argv[-1]).name
                dep = {
                    "incredible_hulk.exe": ["KERNEL32.dll", "rexruntime.dll", "VCRUNTIME140.dll"],
                    "rexruntime.dll": ["vulkan-1.dll", "rexgpu-xenos.dll"],
                    "rexgpu-xenos.dll": ["kernel32.dll", "SDL3.dll"],
                }[name]
                return MockResult("Image has the following dependencies:\n" +
                                  "\n".join("    " + x for x in dep))
            with patch("audit_hulk_runtime_imports.subprocess.run", side_effect=fake_dumpbin):
                report = audit_directory(root)
            self.assertEqual(report["requires_system_vc_redist"], ["vcruntime140.dll"])
            self.assertEqual(report["unresolved_external"], ["sdl3.dll"])
            self.assertEqual(report["binaries"]["incredible_hulk.exe"]["rexruntime.dll"], "app_local")
            self.assertEqual(report["binaries"]["rexruntime.dll"]["vulkan-1.dll"], "vulkan_loader_driver")


if __name__ == "__main__":
    unittest.main()
