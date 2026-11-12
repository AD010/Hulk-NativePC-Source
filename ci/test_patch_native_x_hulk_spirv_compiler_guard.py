#!/usr/bin/env python3
"""Source-only tests for Hulk's NativeX shader compiler safety gate."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from patch_native_x_hulk_spirv_compiler_guard import (
    HEADER_OLD, SHADER_CREATE_OLD, TRANSLATE_BINARY_OLD, TRANSLATE_NAME_OLD,
    MOTION_BINARY_OLD, MOTION_NAME_OLD, PIPELINE_OLD, PIPELINE_NEW,
    VALIDATOR, patch_shaders, patch_pipelines, DRIVER_OLD, DRIVER_NEW, driver_patch_text,
)

class HulkNvidiaCompilerGuardTests(unittest.TestCase):
    def fixture(self):
        return "\n".join((
            HEADER_OLD, TRANSLATE_BINARY_OLD, TRANSLATE_NAME_OLD,
            MOTION_BINARY_OLD, MOTION_NAME_OLD,
            SHADER_CREATE_OLD, SHADER_CREATE_OLD))

    def test_idempotent_atomic(self):
        fixed,c=patch_shaders(self.fixture())
        self.assertTrue(c)
        self.assertEqual(patch_shaders(fixed),(fixed,False))
        self.assertIn("HulkSpirvStructurallyValid",fixed)
        self.assertEqual(fixed.count("BEGIN shader={} words={}"),2)
        self.assertEqual(fixed.count("END shader={} ok={}"),2)
        self.assertIn("return finish(kFailed)",fixed)
        with self.assertRaises(ValueError): patch_shaders(self.fixture().replace(SHADER_CREATE_OLD,"",1))
        with self.assertRaises(ValueError): patch_shaders(fixed.replace("  device_->CreateShader(create_info, &job.host->shader);","",1))
        fixed,c=patch_pipelines(PIPELINE_OLD)
        self.assertTrue(c)
        self.assertEqual(patch_pipelines(fixed),(fixed,False))
        self.assertIn("pixel_shader ? pixel_shader->ucode_data_hash() : 0",fixed)
        self.assertIn("CreateGraphicsPipelineState(create_info",fixed)
        with self.assertRaises(ValueError): patch_pipelines("unknown layout")

    def test_real_driver_patch_applies_and_preserves_call(self):
        # Exercise git's actual patch parser, not just a string presence check.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "Graphics/GraphicsEngineVulkan/src/VulkanUtilities/LogicalDevice.cpp"
            path.parent.mkdir(parents=True)
            path.write_text("// preceding unrelated code\n" + DRIVER_OLD + "// following code\n")
            patch = root / "driver.patch"
            patch.write_text(driver_patch_text())
            subprocess.run(["git", "apply", str(patch)], cwd=root, check=True)
            fixed = path.read_text()
            self.assertEqual(fixed.count("vkCreateGraphicsPipelines("), 1)
            self.assertLess(fixed.index("BEGIN name="), fixed.index("VkResult err ="))
            self.assertLess(fixed.index("VkResult err ="), fixed.index("END name="))
            self.assertLess(fixed.index("END name="), fixed.index("CHECK_VK_ERROR_AND_THROW"))
            self.assertNotIn("catch", fixed)
            self.assertNotIn("PSO_CREATE_FLAG", fixed)
            subprocess.run(["git", "apply", "--reverse", "--check", str(patch)],
                           cwd=root, check=True)

    def test_async_return_is_not_reported_as_compile_success(self):
        self.assertIn("RETURN pipeline={} object_created={}", PIPELINE_NEW)
        self.assertNotIn("END pipeline={} ok={}", PIPELINE_NEW)

    def test_does_not_fake_shaders(self):
        self.assertNotIn("memset",VALIDATOR)
        self.assertNotIn("skip_shader",VALIDATOR)
        self.assertNotIn("allow_invalid",VALIDATOR)
        self.assertIn("instruction_words > count - i",VALIDATOR)
        self.assertIn("words[0] != 0x07230203u",VALIDATOR)

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),"C++ compiler missing")
    def test_compiled_validator_real_function(self):
        cpp=r"""
#include <cstdint>
#include <cstddef>
#include <cassert>
""" + "namespace {\n" + VALIDATOR + "}\n" + r"""
int main() {
  const uint32_t valid[]    = {0x07230203u,0x00010500u,0,16,0,0x00010000u};
  const uint32_t bad_magic[]={0x0135u,0x00010500u,0,16,0,0x00010000u};
  const uint32_t bad_count[]={0x07230203u,0x00010500u,0,16,0,0x00000000u};
  const uint32_t bad_short[]={0x07230203u,0x00010500u,0,16,0,0x00020000u};
  const uint32_t bad_bound[]={0x07230203u,0x00010500u,0,0,0,0x00010000u};
  assert(HulkSpirvStructurallyValid(valid, 6));
  assert(!HulkSpirvStructurallyValid(bad_magic, 6));
  assert(!HulkSpirvStructurallyValid(bad_count, 6));
  assert(!HulkSpirvStructurallyValid(bad_short, 6));
  assert(!HulkSpirvStructurallyValid(bad_bound, 6));
  assert(!HulkSpirvStructurallyValid(valid, 4));
  assert(!HulkSpirvStructurallyValid(nullptr, 6));
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            (p/"test.cpp").write_text(cpp,encoding="utf-8")
            compiler=shutil.which("clang++") or shutil.which("g++")
            exe=p/"test"
            subprocess.run([compiler,"-std=c++20","-Wall","-Wextra","-Werror",str(p/"test.cpp"),"-o",str(exe)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            subprocess.run([str(exe)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)

if __name__=="__main__": unittest.main()
