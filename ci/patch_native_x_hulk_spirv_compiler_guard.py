#!/usr/bin/env python3
"""Hulk #57 NVIDIA compiler crash containment: validate generated SPIR-V and
track the exact shader/pipeline Diligent driver boundary before native Vulkan.

Source-only, no game code/assets, no replacement shader; fail closed on
structurally invalid module. Valid modules pass unchanged. Applies to both
audited NativeXRenderer pins after the existing Hulk graphics patch chain.
"""
from __future__ import annotations

import os
import difflib
from pathlib import Path
import subprocess
import sys

PIN=os.environ.get("HULK_NATIVEX_SOURCE_SHA","bf965e952c98e050753e91d08f32685a9f754b12")
PINS={"bf965e952c98e050753e91d08f32685a9f754b12",
      "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}

VALIDATOR="""// HULK/SPIRV-PREFLIGHT: reject damaged modules BEFORE Vulkan shader creation.
 // This validates SPIR-V section structure and function boundaries only.
 // It is NOT a substitute for the SPIRV-Tools semantic validator.
bool HulkSpirvStructurallyValid(const uint32_t* words, size_t count) {
  if (!words || count < 5 || count > (1u << 20) ||
      words[0] != 0x07230203u || words[4] != 0u ||
      ((words[1] >> 16) & 0xFFu) != 1u || words[3] == 0u)
    return false;
  bool has_capability = false, has_memory_model = false;
  bool has_entrypoint = false, has_function = false, inside_function = false;
  for (size_t i = 5; i < count;) {
    const size_t instruction_words = size_t(words[i] >> 16);
    if (!instruction_words || instruction_words > count - i) return false;
    const uint32_t opcode = words[i] & 0xFFFFu;
    switch (opcode) {
      case 17u: // OpCapability
        if (instruction_words != 2 || inside_function) return false;
        has_capability = true;
        break;
      case 14u: // OpMemoryModel must occur once before functions.
        if (instruction_words != 3 || has_memory_model ||
            inside_function || has_function) return false;
        has_memory_model = true;
        break;
      case 15u: // OpEntryPoint: execution model, function ID, NUL-terminated name.
        if (instruction_words < 4 || inside_function || has_function) return false;
        has_entrypoint = true;
        break;
      case 54u: // OpFunction: result type, result ID, control, function type.
        if (instruction_words != 5 || inside_function ||
            !has_memory_model || !has_entrypoint) return false;
        inside_function = true;
        has_function = true;
        break;
      case 56u: // OpFunctionEnd must close a started function.
        if (instruction_words != 1 || !inside_function) return false;
        inside_function = false;
        break;
      default:
        break;
    }
    i += instruction_words;
  }
  // Previously any header followed by OpNop was accepted; Diligent could
  // submit that byte stream to the NVIDIA compiler as a bogus shader.
  return has_capability && has_memory_model && has_entrypoint &&
         has_function && !inside_function;
}
"""
HEADER_OLD="namespace {\n\n// The SDK's analysis throws"
HEADER_NEW="namespace {\n\n"+VALIDATOR+"// The SDK's analysis throws"
TRANSLATE_BINARY_OLD="""  const std::vector<uint8_t>& binary = translation->translated_binary();
  std::vector<uint32_t> spirv ="""
TRANSLATE_BINARY_NEW="""  const std::vector<uint8_t>& binary = translation->translated_binary();
  // HULK/SPIRV-BYTE-COUNT: a truncated bytecode payload must not be
  // reinterpreted as a whole number of 32-bit SPIR-V words.
  if ((binary.size() & 3u) != 0u) {
    REXLOG_ERROR("[HULK/SPIRV-PREFLIGHT] shader {:016X}: non-word-aligned binary ({} bytes)",
                 shader.ucode_data_hash(), binary.size());
    return finish(kFailed);
  }
  std::vector<uint32_t> spirv ="""
TRANSLATE_NAME_OLD="""  std::string name = std::string(vertex ? "VS " : "PS ") + std::to_string(shader.ucode_data_hash()) + " mod " +"""
TRANSLATE_NAME_NEW="""  if (!HulkSpirvStructurallyValid(spirv.data(), spirv.size())) {
    REXLOG_ERROR("[HULK/SPIRV-PREFLIGHT] malformed {} SPIR-V shader={:016X} mod={:016X} words={}",
                 vertex ? "VS" : "PS", shader.ucode_data_hash(), modification, spirv.size());
    return finish(kFailed);
  }
  std::string name = std::string(vertex ? "VS " : "PS ") + std::to_string(shader.ucode_data_hash()) + " mod " +"""
MOTION_BINARY_OLD="""  const std::vector<uint8_t>& binary = translation->translated_binary();
  std::vector<uint32_t> motion ="""
MOTION_BINARY_NEW="""  const std::vector<uint8_t>& binary = translation->translated_binary();
  if ((binary.size() & 3u) != 0u) {
    REXLOG_ERROR("[HULK/SPIRV-PREFLIGHT] motion VS {:016X}: misaligned binary ({} bytes)",
                 translation->shader().ucode_data_hash(), binary.size());
    return finish(kFailed);
  }
  std::vector<uint32_t> motion ="""
MOTION_NAME_OLD="""  std::string name = "Motion VS " + std::to_string(translation->shader().ucode_data_hash()) + " mod " +"""
MOTION_NAME_NEW="""  if (!HulkSpirvStructurallyValid(spirv.data(), spirv.size())) {
    REXLOG_ERROR("[HULK/SPIRV-PREFLIGHT] malformed motion VS {:016X} mod={:016X} words={}",
                 translation->shader().ucode_data_hash(), translation->modification(), spirv.size());
    return finish(kFailed);
  }
  std::string name = "Motion VS " + std::to_string(translation->shader().ucode_data_hash()) + " mod " +"""
SHADER_CREATE_OLD="""  device_->CreateShader(create_info, &job.host->shader);
  if (!job.host->shader) {"""
SHADER_CREATE_NEW="""  // HULK/NVIDIA-COMPILE-BOUNDARY: paired BEGIN/END probes. The log's
  // existing [t] thread ID and shader hash permit correlation with the
  // owner's NVIDIA driver crash, without logging any copyrighted bytecode.
  REXLOG_INFO("[HULK/NVIDIA-COMPILE-BOUNDARY] BEGIN shader={} words={}", name, spirv.size());
  device_->CreateShader(create_info, &job.host->shader);
  REXLOG_INFO("[HULK/NVIDIA-COMPILE-BOUNDARY] END shader={} ok={}", name, job.host->shader != nullptr);
  if (!job.host->shader) {"""
PIPELINE_OLD="""  auto pipeline = std::make_unique<Pipeline>();
  device_->CreateGraphicsPipelineState(create_info, &pipeline->pso);
  if (!pipeline->pso) {"""
PIPELINE_NEW="""  auto pipeline = std::make_unique<Pipeline>();
  // HULK/NVIDIA-COMPILE-BOUNDARY: identify the exact VS, PS and pipeline
  // requested at the API boundary. RETURN means an object was allocated,
  // NOT that asynchronous driver compilation completed successfully.
  REXLOG_INFO("[HULK/NVIDIA-COMPILE-BOUNDARY] BEGIN pipeline={} VS={:016X} PS={:016X}",
              name, vertex_shader.ucode_data_hash(),
              pixel_shader ? pixel_shader->ucode_data_hash() : 0);
  device_->CreateGraphicsPipelineState(create_info, &pipeline->pso);
  REXLOG_INFO("[HULK/NVIDIA-COMPILE-BOUNDARY] RETURN pipeline={} object_created={}",
              name, pipeline->pso != nullptr);
  if (!pipeline->pso) {"""

DRIVER_PATH = "Graphics/GraphicsEngineVulkan/src/VulkanUtilities/LogicalDevice.cpp"
DRIVER_PATCH_NAME = "0012-hulk-driver-compilation-boundary.patch"
DRIVER_OLD = 'PipelineWrapper LogicalDevice::CreateGraphicsPipeline(const VkGraphicsPipelineCreateInfo& PipelineCI,\n                                                      VkPipelineCache                     cache,\n                                                      const char*                         DebugName) const\n{\n    VERIFY_EXPR(PipelineCI.sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);\n\n    if (DebugName == nullptr)\n        DebugName = "";\n\n    VkPipeline vkPipeline = VK_NULL_HANDLE;\n\n    VkResult err = vkCreateGraphicsPipelines(m_VkDevice, cache, 1, &PipelineCI, m_VkAllocator, &vkPipeline);\n    CHECK_VK_ERROR_AND_THROW(err, "Failed to create graphics pipeline \'", DebugName, \'\\\'\');\n\n    if (*DebugName != 0)\n        SetPipelineName(m_VkDevice, vkPipeline, DebugName);\n\n    return PipelineWrapper{GetSharedPtr(), std::move(vkPipeline)};\n}\n'
DRIVER_NEW = """    // HULK/VK-DRIVER-COMPILE: runs on the actual compiler worker, including
    // library parts, linking and the later whole-pipeline optimization task.
    // NativeX CreateGraphicsPipelineState returns before those tasks finish.
    LOG_INFO_MESSAGE("[HULK/VK-DRIVER-COMPILE] BEGIN name=", DebugName,
                     " flags=", PipelineCI.flags, " stages=", PipelineCI.stageCount);
    VkResult err = vkCreateGraphicsPipelines(m_VkDevice, cache, 1, &PipelineCI, m_VkAllocator, &vkPipeline);
    LOG_INFO_MESSAGE("[HULK/VK-DRIVER-COMPILE] END name=", DebugName,
                     " flags=", PipelineCI.flags, " stages=", PipelineCI.stageCount,
                     " result=", static_cast<int>(err));
"""

def driver_patch_text() -> str:
    # The existing NativeX CMake patch-series loader applies this to the pinned
    # Diligent submodule, after its upstream patches; no second configuration.
    return "".join(difflib.unified_diff(
        DRIVER_OLD.splitlines(keepends=True), DRIVER_OLD.replace(
            "    VkResult err = vkCreateGraphicsPipelines(m_VkDevice, cache, 1, &PipelineCI, m_VkAllocator, &vkPipeline);\n",
            DRIVER_NEW).splitlines(keepends=True),
        fromfile="a/" + DRIVER_PATH, tofile="b/" + DRIVER_PATH))


def replace_one(src: str, old: str, new: str, where: str) -> str:
    n=src.count(old)
    if n!=1:
        raise ValueError(f"{where}: pinned source anchor drift ({n})")
    return src.replace(old,new,1)

def patch_shaders(src: str) -> tuple[str,bool]:
    if (HEADER_NEW in src and SHADER_CREATE_NEW in src and
        src.count(SHADER_CREATE_NEW)==2 and
        TRANSLATE_NAME_NEW in src and MOTION_NAME_NEW in src):
        return src,False
    if ("HULK/NVIDIA-COMPILE-BOUNDARY" in src or
            "HulkSpirvStructurallyValid" in src):
        raise ValueError("Partially installed Hulk NVIDIA shader boundary patch")
    for old, expected in ((HEADER_OLD,1),(TRANSLATE_BINARY_OLD,1),
                          (TRANSLATE_NAME_OLD,1),(MOTION_BINARY_OLD,1),
                          (MOTION_NAME_OLD,1),(SHADER_CREATE_OLD,2)):
        if src.count(old)!=expected:
            raise ValueError("Hulk shader source drift: "+old[:68])
    for old,new in ((HEADER_OLD,HEADER_NEW),
                    (TRANSLATE_BINARY_OLD,TRANSLATE_BINARY_NEW),
                    (TRANSLATE_NAME_OLD,TRANSLATE_NAME_NEW),
                    (MOTION_BINARY_OLD,MOTION_BINARY_NEW),
                    (MOTION_NAME_OLD,MOTION_NAME_NEW)):
        src=replace_one(src,old,new,"shaders.cpp")
    src=src.replace(SHADER_CREATE_OLD,SHADER_CREATE_NEW)
    return src,True

def patch_pipelines(src: str) -> tuple[str,bool]:
    if PIPELINE_NEW in src:
        return src,False
    if "HULK/NVIDIA-COMPILE-BOUNDARY" in src:
        raise ValueError("Partially installed Hulk NVIDIA pipeline boundary patch")
    return replace_one(src,PIPELINE_OLD,PIPELINE_NEW,"pipelines.cpp"),True

def main() -> None:
    if len(sys.argv)!=2:
        raise SystemExit("Usage: patch_native_x_hulk_spirv_compiler_guard.py <NativeX checkout>")
    root=Path(sys.argv[1]).resolve()
    rev=subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if PIN not in PINS or rev!=PIN:
        raise ValueError("Unexpected pinned NativeX commit: "+rev)
    edits=[]
    for name,fn in (("src/shaders.cpp",patch_shaders),
                    ("src/pipelines.cpp",patch_pipelines)):
        path=root/name
        changed,c=fn(path.read_text(encoding="utf-8"))
        edits.append((path,changed,c))
    if len({c for _,_,c in edits})!=1:
        raise ValueError("Partial Hulk shader/pipeline guard installation")
    driver_patch = root / "diligent_patches" / DRIVER_PATCH_NAME
    expected = driver_patch_text()
    if driver_patch.exists() and driver_patch.read_text(encoding="utf-8") != expected:
        raise ValueError("Conflicting Hulk driver boundary patch")
    driver_patch.write_text(expected, encoding="utf-8", newline="\n")
    for path,content,changed in edits:
        if changed: path.write_text(content,encoding="utf-8",newline="\n")
    print("Hulk SPIR-V source validation and NVIDIA shader/pipeline boundaries:",
          "installed" if edits[0][2] else "already installed")

if __name__=="__main__":main()
