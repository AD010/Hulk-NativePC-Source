#!/usr/bin/env python3
"""Prevent native EoR runtime/CI from silently dropping linked source files.

The last Windows Release attempt failed with LNK2019 after a test command
omitted the model materials and native mesh decoder implementations. The
actual native target can similarly compile a Vulkan backend yet omit its
packed VS/PS constants implementation, leaving dead, uncallable EoR code.
This runs before private game data is downloaded, without a release build.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "native_port" / "CMakeLists.txt"
WORKFLOW = ROOT / ".github" / "workflows" / "hulk-free-windows.yml"


class NativeEorLinkCoverage(unittest.TestCase):
    def test_every_eor_native_implementation_linked(self):
        cmake = TARGET.read_text(encoding="utf-8")
        sources = sorted((ROOT / "native_port" / "src").glob("eor_*.cpp"))
        self.assertGreaterEqual(len(sources), 40)
        target = cmake.partition("set(INCREDIBLEHULK_SOURCES")[2]
        target += cmake.partition("target_sources(incredible_hulk PRIVATE src/eor_diligent_mesh_buffers.cpp")[2]
        missing = [source.name for source in sources
                   if not re.search(r"\bsrc/" + re.escape(source.name) + r"\b", target)]
        self.assertEqual(missing, [], f"Native EoR source not linked into EXE: {missing}")

    def test_private_build_model_adapter_links_its_dependents(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        match = re.search(r"\$modelAssetsArgs\s*=\s*@\((.*?)\)\s*\n\s*&\s*cl\.exe\s+@modelAssetsArgs", workflow, re.S)
        self.assertIsNotNone(match, "Original model Windows command changed")
        args = match.group(1)
        for source in (
            "eor_original_model_assets.cpp",
            "eor_model_surface_materials.cpp",
            "eor_native_mesh_geometry.cpp",
            "eor_model_surfaces.cpp",
            "eor_model_resource.cpp",
            "eor_archive.cpp",
        ):
            self.assertIn(source, args,
                          f"Model adapter test cannot link original dependency: {source}")

    def test_host_vulkan_present_avoids_redundant_fullscreen_clear(self):
        host = (ROOT / "native_port" / "src" /
                "eor_diligent_host_output.cpp").read_text(encoding="utf-8")
        self.assertIn("HasFullNativeCompositePixels(", host)
        self.assertIn("desc.Width!=output.width || desc.Height!=output.height", host)
        self.assertIn("Blitter::Source::kColor,", host)
        self.assertIn("device_->Present();", host)
        self.assertNotIn("ClearRenderTarget(", host,
                         "Exact-size host copy must not pre-clear the full swapchain")

    def test_shader_constant_packer_is_both_linked_and_tested(self):
        cmake = TARGET.read_text(encoding="utf-8")
        workflow = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("src/eor_native_shader_constants.cpp", cmake)
        self.assertIn("native_port/src/eor_native_shader_constants.cpp", workflow)
        self.assertIn("ci/eor_native_shader_constants_test.cpp", workflow)


if __name__ == "__main__":
    unittest.main()
