#!/usr/bin/env python3
"""Narrow, fail-closed guard for NativeXRenderer bf965e9 GPU dynamic heap exhaustion.

The Hulk 2026-11-08 minidump proves MapBuffer(MAP_WRITE, DISCARD) returned
nullptr, followed by a 768-byte fetch-constants memcpy to address 0x20.
This guard skips failed draws/overlay uploads. It is NOT a fix for missing
frame boundaries or for the underlying exhausted DiligentCore heap.
Keep upstream pinned; do not silently rebase SDK or alter Raw Input.
"""
import subprocess
import sys
from pathlib import Path

PIN = "bf965e952c98e050753e91d08f32685a9f754b12"

DRAW = [
    (
        "    MappedBuffer map(context, system_constants_buffer_);\n"
        "    std::memcpy(map.data(), &system_constants_, sizeof(system_constants_));",
        "    MappedBuffer map(context, system_constants_buffer_);\n"
        "    if (!map.data()) {\n"
        "      NATIVE_X_RENDERER_WARN_ONCE(\"[native_x_renderer] system constants MapBuffer failed; draw skipped\");\n"
        "      ++stats_.failed;\n"
        "      return false;\n"
        "    }\n"
        "    std::memcpy(map.data(), &system_constants_, sizeof(system_constants_));",
    ),
    (
        "    MappedBuffer map(context, pipeline->float_constants[stage]);\n"
        "    uint8_t* out = map.data();",
        "    MappedBuffer map(context, pipeline->float_constants[stage]);\n"
        "    if (!map.data()) {\n"
        "      NATIVE_X_RENDERER_WARN_ONCE(\"[native_x_renderer] shader constants MapBuffer failed; draw skipped\");\n"
        "      ++stats_.failed;\n"
        "      return false;\n"
        "    }\n"
        "    uint8_t* out = map.data();",
    ),
    (
        "    MappedBuffer map(context, bool_loop_constants_buffer_);\n"
        "    std::memcpy(map.data(), bool_loop, kBoolLoopConstantsSize);",
        "    MappedBuffer map(context, bool_loop_constants_buffer_);\n"
        "    if (!map.data()) {\n"
        "      NATIVE_X_RENDERER_WARN_ONCE(\"[native_x_renderer] bool/loop MapBuffer failed; draw skipped\");\n"
        "      ++stats_.failed;\n"
        "      return false;\n"
        "    }\n"
        "    std::memcpy(map.data(), bool_loop, kBoolLoopConstantsSize);",
    ),
    (
        "    MappedBuffer map(context, fetch_constants_buffer_);\n"
        "    std::memcpy(map.data(), &regs[rg::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], kFetchConstantsSize);",
        "    MappedBuffer map(context, fetch_constants_buffer_);\n"
        "    if (!map.data()) {\n"
        "      NATIVE_X_RENDERER_WARN_ONCE(\"[native_x_renderer] fetch constants MapBuffer failed (768 bytes); draw skipped\");\n"
        "      ++stats_.failed;\n"
        "      return false;\n"
        "    }\n"
        "    std::memcpy(map.data(), &regs[rg::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], kFetchConstantsSize);",
    ),
    (
        "      auto upload = [&](IBuffer* buffer, size_t size) {\n"
        "        MappedBuffer map(context, buffer);\n"
        "        std::memcpy(map.data(), data, size);\n"
        "        data += size;\n"
        "      };",
        "      auto upload = [&](IBuffer* buffer, size_t size) -> bool {\n"
        "        MappedBuffer map(context, buffer);\n"
        "        if (!map.data()) return false;\n"
        "        std::memcpy(map.data(), data, size);\n"
        "        data += size;\n"
        "        return true;\n"
        "      };",
    ),
    (
        "      upload(pipeline.float_constants[0], draw.constants_size);\n"
        "      upload(pipeline.previous_float_constants, draw.constants_size);\n"
        "      upload(system_constants_buffer_, sizeof(system_constants_));\n"
        "      upload(bool_loop_constants_buffer_, kBoolLoopConstantsSize);\n"
        "      upload(fetch_constants_buffer_, kFetchConstantsSize);",
        "      if (!upload(pipeline.float_constants[0], draw.constants_size) ||\n"
        "          !upload(pipeline.previous_float_constants, draw.constants_size) ||\n"
        "          !upload(system_constants_buffer_, sizeof(system_constants_)) ||\n"
        "          !upload(bool_loop_constants_buffer_, kBoolLoopConstantsSize) ||\n"
        "          !upload(fetch_constants_buffer_, kFetchConstantsSize)) {\n"
        "        NATIVE_X_RENDERER_WARN_ONCE(\"[native_x_renderer] motion upload MapBuffer failed; draw skipped\");\n"
        "        continue;\n"
        "      }",
    ),
    (
        "        MappedBuffer map(context, motion_constants_buffer_);\n"
        "        std::memcpy(map.data(), ndc_to_pixels, sizeof(ndc_to_pixels));",
        "        MappedBuffer map(context, motion_constants_buffer_);\n"
        "        if (!map.data()) {\n"
        "          NATIVE_X_RENDERER_WARN_ONCE(\"[native_x_renderer] motion constants MapBuffer failed; draw skipped\");\n"
        "          continue;\n"
        "        }\n"
        "        std::memcpy(map.data(), ndc_to_pixels, sizeof(ndc_to_pixels));",
    ),
]

OVERLAY = [
    (
        "  context->MapBuffer(vertices_, MAP_WRITE, MAP_FLAG_DISCARD, data);\n"
        "  std::memcpy(data, frame.vertices.data(), vertex_bytes);",
        "  context->MapBuffer(vertices_, MAP_WRITE, MAP_FLAG_DISCARD, data);\n"
        "  if (!data) return;  // DiligentCore already logged the failed map.\n"
        "  std::memcpy(data, frame.vertices.data(), vertex_bytes);",
    ),
    (
        "  context->MapBuffer(indices_, MAP_WRITE, MAP_FLAG_DISCARD, data);\n"
        "  std::memcpy(data, frame.indices.data(), index_bytes);",
        "  data = nullptr;\n"
        "  context->MapBuffer(indices_, MAP_WRITE, MAP_FLAG_DISCARD, data);\n"
        "  if (!data) return;\n"
        "  std::memcpy(data, frame.indices.data(), index_bytes);",
    ),
    (
        "  context->MapBuffer(constants_, MAP_WRITE, MAP_FLAG_DISCARD, data);\n"
        "  float scale_offset[4] = {2.0f / frame.width, -2.0f / frame.height, -1.0f, 1.0f};\n"
        "  std::memcpy(data, scale_offset, sizeof(scale_offset));",
        "  data = nullptr;\n"
        "  context->MapBuffer(constants_, MAP_WRITE, MAP_FLAG_DISCARD, data);\n"
        "  if (!data) return;\n"
        "  float scale_offset[4] = {2.0f / frame.width, -2.0f / frame.height, -1.0f, 1.0f};\n"
        "  std::memcpy(data, scale_offset, sizeof(scale_offset));",
    ),
]


def patch_text(text: str, rules: list[tuple[str, str]]) -> tuple[str, bool]:
    states = [new in text for _, new in rules]
    if all(states):
        return text, False  # idempotent, even when an existing SDK checkout is reused
    if any(states):
        raise ValueError("Partially patched NativeXRenderer file; refusing unsafe update")
    for old, new in rules:
        if old == new or text.count(old) != 1:
            raise ValueError("Pinned NativeXRenderer upstream anchors changed; refusing update")
    result = text
    for old, new in rules:
        result = result.replace(old, new)
    return result, True


def patch_file(path: Path, rules: list[tuple[str, str]]) -> bool:
    original = path.read_text(encoding="utf-8")
    patched, changed = patch_text(original, rules)
    if changed:
        path.write_text(patched, encoding="utf-8", newline="\n")
    return changed


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_heap_guard.py <pinned NativeXRenderer checkout>")
    root = Path(sys.argv[1]).resolve()
    head = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if head != PIN:
        raise ValueError(f"Unexpected NativeXRenderer SHA {head}; expected {PIN}")
    pairs = [(root / "src/draw.cpp", DRAW), (root / "src/overlay.cpp", OVERLAY)]
    # Preflight BOTH files before writing either one.
    output = [(path, *patch_text(path.read_text(encoding="utf-8"), rules)) for path, rules in pairs]
    for path, patched, changed in output:
        if changed:
            path.write_text(patched, encoding="utf-8", newline="\n")
        print("NativeX heap guard:", path.name, "patched" if changed else "already installed")


if __name__ == "__main__":
    main()
