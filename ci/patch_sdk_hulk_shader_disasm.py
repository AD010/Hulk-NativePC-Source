#!/usr/bin/env python3
"""Fix a nullable Xenos vertex format name in the ReXGlue shader disassembler.

NativeXRenderer's ShaderCache catches fmt::format_error("string pointer is null")
and rejects the whole shader. Some Xbox 360 formats have no name in the
diagnostic kVertexFetchDataFormats table; a null C-string must never be
passed to StringBuffer::AppendFormat. Preserve shader format IDs and
runtime translation. This does NOT fix unrelated pixel-state mismatches.
Exact upstream anchor; fail closed for an altered SDK source.
"""
from pathlib import Path
import sys

FILE = "src/graphics/pipeline/shader/translator_disasm.cpp"
OLD = "  if (attributes.data_format != xenos::VertexFormat::kUndefined) {\n    out->AppendFormat(\", DataFormat={}\",\n                      kVertexFetchDataFormats[static_cast<int>(attributes.data_format)].name);\n  }"
NEW = "  if (attributes.data_format != xenos::VertexFormat::kUndefined) {\n    // The Xenos format table has unnamed slots: passing their null names to\n    // fmt throws \"string pointer is null\" and aborts shader *analysis*.\n    // Disassembly is for diagnostics; an unknown name must not make the\n    // underlying GPU microcode fail its translation.\n    const uint32_t format_id = static_cast<uint32_t>(attributes.data_format);\n    const char* format_name =\n        format_id < sizeof(kVertexFetchDataFormats) / sizeof(kVertexFetchDataFormats[0])\n            ? kVertexFetchDataFormats[format_id].name : nullptr;\n    if (format_name) {\n      out->AppendFormat(\", DataFormat={}\", format_name);\n    } else {\n      out->AppendFormat(\", DataFormat=unlisted(0x{:X})\", format_id);\n    }\n  }"


def patch_text(data: str) -> tuple[str, bool]:
    if data.count(NEW) == 1 and OLD not in data:
        return data, False
    if data.count(OLD) != 1 or NEW in data:
        raise ValueError("ReXGlue vertex-format disassembler changed or partial patch")
    return data.replace(OLD, NEW, 1), True


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_sdk_hulk_shader_disasm.py <rexglue-sdk>")
    path = Path(sys.argv[1]) / FILE
    updated, changed = patch_text(path.read_text(encoding="utf-8-sig"))
    if changed:
        path.write_text(updated, encoding="utf-8", newline="\n")
    print("Hulk Xenos nullable vertex-format name:", "fixed" if changed else "already patched")


if __name__ == "__main__":
    main()
