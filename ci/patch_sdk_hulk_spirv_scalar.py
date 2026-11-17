#!/usr/bin/env python3
"""Port the Xenia Edge 09d405f scalar SPIR-V fetch fix to Hulk's pinned ReXGlue.

The pinned ReXGlue 'recomp' translator still emits OpVectorTimesScalar for
32-bit one-component vertex fetch normalization and exp_adjust. This is an
invalid scalar operand and can crash NVIDIA's SPIR-V driver compiler.
Edit only the exact verified code paths and bump the translator cache version.
Run AFTER Hulk's fetch-word3 and computed-LOD patches (version 14).
No guessed game/shader data and no global NativeX renderer replacement.
"""
from __future__ import annotations

from pathlib import Path
import sys

SHADER = "src/graphics/pipeline/shader/spirv_translator_fetch.cpp"
HEADER = "include/rex/graphics/pipeline/shader/spirv_translator.h"
MARKER = "HULK_SPIRV_SCALAR_FETCH_XENIA_EDGE_09D405F"
VERSION_OLD = "    static constexpr uint32_t kVersion = 14;"
VERSION_NEW = "    static constexpr uint32_t kVersion = 15;"
START = "    case xenos::VertexFormat::k_32:\n"
END = "    case xenos::VertexFormat::k_32_FLOAT:\n"
NORMALIZE = "      if (!instr.attributes.is_integer) {\n        if (instr.attributes.is_signed) {"
INSERT = """      if (!instr.attributes.is_integer) {
        // HULK_SPIRV_SCALAR_FETCH_XENIA_EDGE_09D405F:
        // OpVectorTimesScalar requires a VECTOR, never a one-component
        // scalar produced by Xenos 32-bit normalized vertex fetch.
        const spv::Op normalized_mul_op = used_format_component_count > 1
            ? spv::OpVectorTimesScalar : spv::OpFMul;
        if (instr.attributes.is_signed) {"""
EXPO_OLD = """    if (instr.attributes.exp_adjust) {
      result = builder_->createNoContractionBinOp(
          spv::OpVectorTimesScalar, builder_->getTypeId(result), result,
          builder_->makeFloatConstant(std::ldexp(1.0f, instr.attributes.exp_adjust)));
    }"""
EXPO_NEW = """    if (instr.attributes.exp_adjust) {
      // The original Xenos float result is still at the fetch format's
      // used component width here (result padding happens AFTER exp_adjust).
      // Use the verified source's known component count: SpvBuilder has no
      // isVectorType method in this pinned glslang API.
      const spv::Id adjusted_type = builder_->getTypeId(result);
      result = builder_->createNoContractionBinOp(
          used_format_component_count > 1 ? spv::OpVectorTimesScalar
                                          : spv::OpFMul,
          adjusted_type, result,
          builder_->makeFloatConstant(std::ldexp(1.0f, instr.attributes.exp_adjust)));
    }"""


def patch(shader: str, header: str) -> tuple[str, str, bool]:
    if MARKER in shader or VERSION_NEW in header:
        if (MARKER not in shader or VERSION_NEW not in header or
                shader.count("normalized_mul_op") != 4 or
                shader.count(EXPO_NEW) != 1):
            raise ValueError("Partially patched ReXGlue scalar vertex fetch")
        return shader, header, False
    if header.count(VERSION_OLD) != 1 or shader.count(START) != 1 or shader.count(END) != 1:
        raise ValueError("Unknown pinned ReXGlue shader translator/cache version")
    before, sep, rest = shader.partition(START)
    block, stop, after = rest.partition(END)
    if not sep or not stop or block.count(NORMALIZE) != 1 or (
            block.count("spv::OpVectorTimesScalar") != 3 or
            block.count("spv::OpFMul") != 0 or
            shader.count(EXPO_OLD) != 1):
        raise ValueError("Xenos 32-bit normalized fetch/exponent ABI changed")
    rewritten = block.replace(NORMALIZE, INSERT, 1).replace(
        "spv::OpVectorTimesScalar, result_type, result",
        "normalized_mul_op, result_type, result")
    if rewritten.count("normalized_mul_op") != 4:
        raise ValueError("Not all scalar normalization paths patched")
    new_shader = (before + START + rewritten + END + after).replace(
        EXPO_OLD, EXPO_NEW, 1)
    new_header = header.replace(VERSION_OLD, VERSION_NEW, 1)
    return new_shader, new_header, True


def apply(root: Path) -> None:
    path, hdr = root / SHADER, root / HEADER
    shader, header, changed = patch(
        path.read_text(encoding="utf-8-sig"),
        hdr.read_text(encoding="utf-8-sig"))
    if changed:
        path.write_text(shader, encoding="utf-8", newline="\n")
        hdr.write_text(header, encoding="utf-8", newline="\n")
    print("Hulk/Xenia Edge scalar vertex SPIR-V: " +
          ("installed (cache version 15)" if changed else "already installed"))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_sdk_hulk_spirv_scalar.py SDK_ROOT")
    apply(Path(sys.argv[1]))
