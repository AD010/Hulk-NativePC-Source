#!/usr/bin/env python3
"""Preserve pixel-shader getCompTexLOD as native SPIR-V query in Hulk 176.

Apply AFTER the existing Hulk word-3 exponent source patch (version 12->13).
Contradictory optional LOD/gradient flags do not make the pixel opcode a
constant-zero operation. Vertex cases retain the upstream safe fallback.
"""
from pathlib import Path
import sys

SHADER="src/graphics/pipeline/shader/spirv_translator_fetch.cpp"
HEADER="include/rex/graphics/pipeline/shader/spirv_translator.h"
VERSION_OLD="    static constexpr uint32_t kVersion = 13;"
VERSION_NEW="    static constexpr uint32_t kVersion = 14;"
ORIGINAL="    if (instr.opcode == ucode::FetchOpcode::kGetTextureComputedLod &&\n        (!use_computed_lod || instr.attributes.use_register_gradients)) {\n      assert_always();\n      EmitTranslationError(\n          \"getCompTexLOD used with explicit LOD or gradients - contradicts \"\n          \"MSDN\",\n          false);\n      StoreResult(instr.result, const_float_vectors_0_[used_result_component_count - 1]);\n      return;\n    }\n"
FIXED="    if (instr.opcode == ucode::FetchOpcode::kGetTextureComputedLod &&\n        (!use_computed_lod || instr.attributes.use_register_gradients)) {\n      // HULK/COMP-LOD-NATIVE: getCompTexLOD in a pixel shader may contain\n      // contradictory explicit-LOD/gradient flags. The opcode asks for a\n      // COMPUTED mip level. Let the existing QueryTextureLod path below\n      // evaluate implicit pixel derivatives instead of writing a fake zero.\n      // The vertex stage still has no legal implicit derivatives.\n      if (is_pixel_shader()) {\n        use_computed_lod = true;\n      } else {\n        assert_always();\n        EmitTranslationError(\n            \"getCompTexLOD requires pixel shader implicit derivatives\", false);\n        StoreResult(instr.result, const_float_vectors_0_[used_result_component_count - 1]);\n        return;\n      }\n    }\n"

def patch(shader: str, header: str) -> tuple[str,str,bool]:
    if FIXED in shader and VERSION_NEW in header:
        return shader,header,False
    if "HULK/COMP-LOD-NATIVE" in shader or VERSION_NEW in header:
        raise ValueError("Partial Hulk computed LOD patch")
    if shader.count(ORIGINAL)!=1 or header.count(VERSION_OLD)!=1:
        raise ValueError("Unknown ReXGlue getCompTexLOD translator source")
    if ("if (instr.opcode == ucode::FetchOpcode::kGetTextureComputedLod) {" not in shader
        or "QueryTextureLod(texture_parameters," not in shader
        or "spv::OpImageQueryLod" not in shader):
        raise ValueError("Existing native SPIR-V texture LOD query unavailable")
    return shader.replace(ORIGINAL,FIXED,1), header.replace(VERSION_OLD,VERSION_NEW,1), True

def main() -> None:
    if len(sys.argv)!=2:
        raise SystemExit("Usage: patch_sdk_hulk_computed_lod.py SDK_ROOT")
    root=Path(sys.argv[1])
    p=root/SHADER
    h=root/HEADER
    shader,header,changed=patch(p.read_text(encoding="utf-8-sig"),h.read_text(encoding="utf-8-sig"))
    if changed:
        p.write_text(shader,encoding="utf-8",newline="\n")
        h.write_text(header,encoding="utf-8",newline="\n")
    print("Hulk computed LOD native SPIR-V:", "installed" if changed else "already installed")

if __name__=="__main__":
    main()
