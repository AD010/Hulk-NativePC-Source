#!/usr/bin/env python3
"""Validated, source-pinned Xenia Edge backports for the ReXGlue Hulk SDK.

One cohesive adapter owns the shader scalar, converted-index pool, and packed
mip0/2x1 corrections. Invoke independently at their original build stages:
  python ci/patch_sdk_hulk_xenia_edge.py index-pool SDK_ROOT
  python ci/patch_sdk_hulk_xenia_edge.py packed-mip SDK_ROOT
  python ci/patch_sdk_hulk_xenia_edge.py spirv-scalar SDK_ROOT

All patches match reviewed original source lines, reject unknown versions,
and never use guessed shader/texture addresses. No game assets included.
"""
from __future__ import annotations

import argparse
from pathlib import Path

# --- Xenia Edge index original verified adaptation ---
INDEX_FILE = "include/rex/graphics/primitive_processor.h"
INDEX_MARKER = "HULK/XENIA-EDGE-INDEX-PAGE-4AD875"
INDEX_OLD = "      sizeof(uint32_t) * (UINT16_MAX - 2) * 3 * +XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE;"
INDEX_NEW = """      // HULK/XENIA-EDGE-INDEX-PAGE-4AD875: SIMD is padding, not a factor.
      sizeof(uint32_t) * (UINT16_MAX - 2) * 3 + XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE;"""


def patch_index(source: str) -> tuple[str, bool]:
    if INDEX_MARKER in source:
        if (source.count(INDEX_MARKER) != 1 or source.count(INDEX_NEW) != 1 or
                INDEX_OLD in source):
            raise ValueError("Partial converted-index page fix")
        return source, False
    if source.count(INDEX_OLD) != 1 or "kMinRequiredConvertedIndexBufferSize" not in source:
        raise ValueError("Pinned ReXGlue primitive pool expression changed")
    return source.replace(INDEX_OLD, INDEX_NEW, 1), True


def apply_index(root: Path) -> None:
    path = root / INDEX_FILE
    fixed, changed = patch_index(path.read_text(encoding="utf-8-sig"))
    if changed:
        path.write_text(fixed, encoding="utf-8", newline="\n")
    print("Hulk/Xenia Edge converted index pool: " +
          ("fixed (SIMD alignment added)" if changed else "already fixed"))

# --- Xenia Edge mip original verified adaptation ---
MIP_FILE = "src/graphics/pipeline/texture/util.cpp"
MIP_MARKER = "HULK/XENIA-EDGE-PACKED-MIPS-344FCA7"
MIP_ADDR_OLD = """  uint32_t base_page = fetch.base_address & 0x1FFFF;
  uint32_t mip_page = fetch.mip_address & 0x1FFFF;

  uint32_t mip_min_level, mip_max_level;"""
MIP_ADDR_NEW = """  uint32_t base_page = fetch.base_address & 0x1FFFF;
  uint32_t mip_page = fetch.mip_address & 0x1FFFF;
  // HULK/XENIA-EDGE-PACKED-MIPS-344FCA7: when mip 0 itself is packed,
  // ALL levels belong to the base image page. mip_address may be nonzero.
  if (base_page != 0 && fetch.packed_mips &&
      fetch.dimension != xenos::DataDimension::k1D &&
      GetPackedMipLevel(width_minus_1 + 1, height_minus_1 + 1) == 0) {
    mip_page = base_page;
  }

  uint32_t mip_min_level, mip_max_level;"""
MIP_OFFSET_START = "bool GetPackedMipOffset("
MIP_OFFSET_END = "  const FormatInfo* format_info = FormatInfo::Get(format);"
MIP_SHAPE_OLD = "    if (log2_width > log2_height) {"
MIP_SHAPE_NEW = "    if (is_wide) {"
MIP_SHAPE_INSERT_OLD = """  uint32_t packed_mip = mip - packed_mip_base;

  // Find the block offset of the mip."""
MIP_SHAPE_INSERT_NEW = """  uint32_t packed_mip = mip - packed_mip_base;

  // 2x1 follows square Xenos packed layout (not the wide-rectangle one).
  // The square/2x1 boundary is a hardware-observed source rule.
  const bool is_wide = log2_width > std::max(log2_height, uint32_t(1));

  // Find the block offset of the mip."""


def patch_mip(source: str) -> tuple[str, bool]:
    if MIP_MARKER in source:
        # Idempotence must verify the complete packed-tail transformation.
        # Merely finding two "is_wide" uses anywhere in the file can hide
        # a partial edit inside GetPackedMipOffset.
        if (source.count(MIP_MARKER) != 1 or
                source.count(MIP_ADDR_NEW) != 1 or MIP_ADDR_OLD in source or
                source.count(MIP_OFFSET_START) != 1 or
                source.count(MIP_OFFSET_END) != 1):
            raise ValueError("Partial source-proven Xenos packed-mip fix")
        _, _, tail = source.partition(MIP_OFFSET_START)
        block, _, _ = tail.partition(MIP_OFFSET_END)
        if (block.count(MIP_SHAPE_INSERT_NEW) != 1 or
                block.count(MIP_SHAPE_NEW) != 2 or
                MIP_SHAPE_INSERT_OLD in block or MIP_SHAPE_OLD in block):
            raise ValueError("Partial packed-tail shape transformation")
        return source, False
    if source.count(MIP_ADDR_OLD) != 1 or source.count(MIP_OFFSET_START) != 1 or (
            source.count(MIP_OFFSET_END) != 1):
        raise ValueError("Unknown pinned ReXGlue texture-fetch implementation")
    before, _, remaining = source.partition(MIP_OFFSET_START)
    block, _, after = remaining.partition(MIP_OFFSET_END)
    if (not block or block.count(MIP_SHAPE_INSERT_OLD) != 1 or
            block.count(MIP_SHAPE_OLD) != 2 or
            "GetPackedMipOffset" in block):
        raise ValueError("Unknown Xenos packed tail shape calculations")
    new_block = block.replace(MIP_SHAPE_INSERT_OLD, MIP_SHAPE_INSERT_NEW, 1)
    new_block = new_block.replace(MIP_SHAPE_OLD, MIP_SHAPE_NEW)
    output = (before + MIP_OFFSET_START + new_block + MIP_OFFSET_END + after).replace(
        MIP_ADDR_OLD, MIP_ADDR_NEW, 1)
    return output, True


def apply_mip(root: Path) -> None:
    path = root / MIP_FILE
    output, changed = patch_mip(path.read_text(encoding="utf-8-sig"))
    if changed:
        path.write_text(output, encoding="utf-8", newline="\n")
    print("Hulk/Xenia Edge authentic packed mip: " +
          ("fixed" if changed else "already installed"))

# --- Xenia Edge scalar original verified adaptation ---
SCALAR_SHADER = "src/graphics/pipeline/shader/spirv_translator_fetch.cpp"
SCALAR_HEADER = "include/rex/graphics/pipeline/shader/spirv_translator.h"
SCALAR_MARKER = "HULK_SPIRV_SCALAR_FETCH_XENIA_EDGE_09D405F"
SCALAR_VERSION_OLD = "    static constexpr uint32_t kVersion = 14;"
SCALAR_VERSION_NEW = "    static constexpr uint32_t kVersion = 15;"
SCALAR_START = "    case xenos::VertexFormat::k_32:\n"
SCALAR_END = "    case xenos::VertexFormat::k_32_FLOAT:\n"
SCALAR_NORMALIZE = "      if (!instr.attributes.is_integer) {\n        if (instr.attributes.is_signed) {"
SCALAR_INSERT = """      if (!instr.attributes.is_integer) {
        // HULK_SPIRV_SCALAR_FETCH_XENIA_EDGE_09D405F:
        // OpVectorTimesScalar requires a VECTOR, never a one-component
        // scalar produced by Xenos 32-bit normalized vertex fetch.
        const spv::Op normalized_mul_op = used_format_component_count > 1
            ? spv::OpVectorTimesScalar : spv::OpFMul;
        if (instr.attributes.is_signed) {"""
SCALAR_EXPO_OLD = """    if (instr.attributes.exp_adjust) {
      result = builder_->createNoContractionBinOp(
          spv::OpVectorTimesScalar, builder_->getTypeId(result), result,
          builder_->makeFloatConstant(std::ldexp(1.0f, instr.attributes.exp_adjust)));
    }"""
SCALAR_EXPO_NEW = """    if (instr.attributes.exp_adjust) {
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


def patch_scalar(shader: str, header: str) -> tuple[str, str, bool]:
    if SCALAR_MARKER in shader or SCALAR_VERSION_NEW in header:
        # Validate the transformed fetch case itself, not unrelated text
        # elsewhere in the translator (which could mask a partial edit).
        before, sep, rest = shader.partition(SCALAR_START)
        block, stop, after = rest.partition(SCALAR_END)
        if (not sep or not stop or shader.count(SCALAR_START) != 1 or
                shader.count(SCALAR_END) != 1 or
                shader.count(SCALAR_MARKER) != 1 or
                header.count(SCALAR_VERSION_NEW) != 1 or
                SCALAR_VERSION_OLD in header or
                block.count(SCALAR_MARKER) != 1 or
                block.count("normalized_mul_op, result_type, result") != 3 or
                block.count("const spv::Op normalized_mul_op") != 1 or
                SCALAR_NORMALIZE in block or
                block.count("spv::OpVectorTimesScalar, result_type, result") != 0 or
                shader.count(SCALAR_EXPO_NEW) != 1 or
                SCALAR_EXPO_OLD in shader):
            raise ValueError("Partially patched ReXGlue scalar vertex fetch")
        return shader, header, False
    if header.count(SCALAR_VERSION_OLD) != 1 or shader.count(SCALAR_START) != 1 or shader.count(SCALAR_END) != 1:
        raise ValueError("Unknown pinned ReXGlue shader translator/cache version")
    before, sep, rest = shader.partition(SCALAR_START)
    block, stop, after = rest.partition(SCALAR_END)
    if not sep or not stop or block.count(SCALAR_NORMALIZE) != 1 or (
            block.count("spv::OpVectorTimesScalar") != 3 or
            block.count("spv::OpFMul") != 0 or
            shader.count(SCALAR_EXPO_OLD) != 1):
        raise ValueError("Xenos 32-bit normalized fetch/exponent ABI changed")
    rewritten = block.replace(SCALAR_NORMALIZE, SCALAR_INSERT, 1).replace(
        "spv::OpVectorTimesScalar, result_type, result",
        "normalized_mul_op, result_type, result")
    if rewritten.count("normalized_mul_op") != 4:
        raise ValueError("Not all scalar normalization paths patched")
    new_shader = (before + SCALAR_START + rewritten + SCALAR_END + after).replace(
        SCALAR_EXPO_OLD, SCALAR_EXPO_NEW, 1)
    new_header = header.replace(SCALAR_VERSION_OLD, SCALAR_VERSION_NEW, 1)
    return new_shader, new_header, True


def apply_scalar(root: Path) -> None:
    path, hdr = root / SCALAR_SHADER, root / SCALAR_HEADER
    shader, header, changed = patch_scalar(
        path.read_text(encoding="utf-8-sig"),
        hdr.read_text(encoding="utf-8-sig"))
    if changed:
        path.write_text(shader, encoding="utf-8", newline="\n")
        hdr.write_text(header, encoding="utf-8", newline="\n")
    print("Hulk/Xenia Edge scalar vertex SPIR-V: " +
          ("installed (cache version 15)" if changed else "already installed"))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("index-pool", "packed-mip", "spirv-scalar"))
    parser.add_argument("sdk_root", type=Path)
    args = parser.parse_args()
    if not args.sdk_root.is_dir():
        parser.error("Pinned ReXGlue SDK source directory does not exist")
    if args.mode == "index-pool":
        apply_index(args.sdk_root)
    elif args.mode == "packed-mip":
        apply_mip(args.sdk_root)
    else:
        apply_scalar(args.sdk_root)


if __name__ == "__main__":
    main()
