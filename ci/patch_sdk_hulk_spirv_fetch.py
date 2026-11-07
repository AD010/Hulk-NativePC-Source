#!/usr/bin/env python3
"""Narrow Hulk-side adaptation of NeverCookFirst/rexglue-sdk 0132a6a41e6b.

Only fixes the Vulkan/SPIR-V fetch result exponent source (Xenos word 3, not
LOD-bias word 4) and invalidates compiled shader caches by bumping the SDK's
translator version. No wholesale SDK replacement, no implicit integer scaling.
Rejects unknown upstream layouts rather than corrupting the SDK.
"""
from __future__ import annotations

import pathlib
import sys

SHADER = "src/graphics/pipeline/shader/spirv_translator_fetch.cpp"
HEADER = "include/rex/graphics/pipeline/shader/spirv_translator.h"
MARKER = "HULK_SPIRV_FETCH_WORD3"

WORD4 = """        spv::Id fetch_constant_word_4_signed =
            builder_->createUnaryOp(spv::OpBitcast, type_int_, fetch_constant_word_4);
"""
LOAD3 = """
        // HULK_SPIRV_FETCH_WORD3: Xenos result exponent bias is in word 3,
        // not the word 4 LOD bias. Ported narrowly from NeverCookFirst 0132a6a.
        id_vector_temp_.clear();
        id_vector_temp_.push_back(const_int_0_);
        id_vector_temp_.push_back(
            builder_->makeIntConstant(int((fetch_constant_word_0_index + 3) >> 2)));
        id_vector_temp_.push_back(
            builder_->makeIntConstant(int((fetch_constant_word_0_index + 3) & 3)));
        spv::Id fetch_constant_word_3_signed = builder_->createUnaryOp(
            spv::OpBitcast, type_int_,
            builder_->createLoad(builder_->createAccessChain(spv::StorageClassUniform,
                                                             uniform_fetch_constants_,
                                                             id_vector_temp_),
                                 spv::NoPrecision));
"""
EXP_OLD = """            builder_->createTriOp(spv::OpBitFieldSExtract, type_int_, fetch_constant_word_4_signed,
                                  builder_->makeUintConstant(13), builder_->makeUintConstant(6)))"""
COMMENT_OLD = """        // Apply the exponent bias from the bits 13:18 of the fetch constant
        // word 4."""
COMMENT_NEW = "        // Apply the exponent bias from bits 13:18 of texture-fetch word 3."
VERSION_OLD = "    static constexpr uint32_t kVersion = 12;"
VERSION_NEW = "    static constexpr uint32_t kVersion = 13;"


def patch(shader: str, header: str) -> tuple[str, str, bool]:
    if MARKER in shader:
        if VERSION_NEW not in header or "fetch_constant_word_3_signed" not in shader:
            raise ValueError("Partially patched Vulkan translator")
        return shader, header, False
    for anchor, data in ((WORD4, shader), (EXP_OLD, shader),
                         (COMMENT_OLD, shader), (VERSION_OLD, header)):
        if data.count(anchor) != 1:
            raise ValueError(f"SDK layout changed: expected exactly one {anchor[:60]!r}")
    updated_shader = shader.replace(WORD4, WORD4 + LOAD3, 1)
    updated_shader = updated_shader.replace(
        EXP_OLD, EXP_OLD.replace("fetch_constant_word_4_signed",
                                 "fetch_constant_word_3_signed"), 1)
    updated_shader = updated_shader.replace(COMMENT_OLD, COMMENT_NEW, 1)
    updated_header = header.replace(VERSION_OLD, VERSION_NEW, 1)
    return updated_shader, updated_header, True


def apply(root: pathlib.Path) -> None:
    shader_path, header_path = root / SHADER, root / HEADER
    original_shader = shader_path.read_text(encoding="utf-8-sig")
    original_header = header_path.read_text(encoding="utf-8-sig")
    shader, header, changed = patch(original_shader, original_header)
    if changed:
        shader_path.write_text(shader, encoding="utf-8", newline="\n")
        header_path.write_text(header, encoding="utf-8", newline="\n")
    print("Hulk Vulkan fetch exponent word3 fix: " +
          ("installed (shader cache version 13)" if changed else "already installed"))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_sdk_hulk_spirv_fetch.py SDK_ROOT")
    apply(pathlib.Path(sys.argv[1]))
