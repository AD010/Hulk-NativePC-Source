#!/usr/bin/env python3
"""Hulk #57 NVIDIA compiler crash containment: validate generated SPIR-V and
track the exact shader/pipeline Diligent driver boundary before native Vulkan.

Source-only, no game code/assets, no replacement shader; fail closed on
structurally invalid module. Valid modules pass unchanged. Applies to both
audited NativeXRenderer pins after the existing Hulk graphics patch chain.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

PIN=os.environ.get("HULK_NATIVEX_SOURCE_SHA","bf965e952c98e050753e91d08f32685a9f754b12")
PINS={"bf965e952c98e050753e91d08f32685a9f754b12",
      "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}

VALIDATOR="""// HULK/SPIRV-PREFLIGHT: a minimum, memory-safe SPIR-V instruction
// word-stream check before passing code to the vendor driver. This is
// deliberately NOT a substitute for SPIRV-Tools semantic validation.
bool HulkSpirvStructurallyValid(const uint32_t* words, size_t count) {
  if (!words || count < 5 || count > (1u << 20) ||
      words[0] != 0x07230203u || words[4] != 0u ||
      ((words[1] >> 16) & 0xFFu) != 1u || words[3] == 0u)
    return false;
  for (size_t i = 5; i < count;) {
    const size_t instruction_words = size_t(words[i] >> 16);
    if (!instruction_words || instruction_words > count - i) return false;
    i += instruction_words;
  }
  return true;
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
  // involved when the driver reports a fault on its background compiler
  // thread. Unmatched BEGIN is suggestive, not proof of the root cause.
  REXLOG_INFO("[HULK/NVIDIA-COMPILE-BOUNDARY] BEGIN pipeline={} VS={:016X} PS={:016X}",
              name, vertex_shader.ucode_data_hash(),
              pixel_shader ? pixel_shader->ucode_data_hash() : 0);
  device_->CreateGraphicsPipelineState(create_info, &pipeline->pso);
  REXLOG_INFO("[HULK/NVIDIA-COMPILE-BOUNDARY] END pipeline={} ok={}",
              name, pipeline->pso != nullptr);
  if (!pipeline->pso) {"""

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
    if "HULK/NVIDIA-COMPILE-BOUNDARY" in src or
        "HulkSpirvStructurallyValid" in src:
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
    for path,content,changed in edits:
        if changed: path.write_text(content,encoding="utf-8",newline="\n")
    print("Hulk SPIR-V source validation and NVIDIA shader/pipeline boundaries:",
          "installed" if edits[0][2] else "already installed")

if __name__=="__main__":main()
