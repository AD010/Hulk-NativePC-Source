#!/usr/bin/env python3
"""Bounds-check Xenos CF EXEC spans before shader instruction dereference.

Log195 has two 27-dword vertex shader analysis access violations from
IM_LOAD_IMMEDIATE. SDK parsed EXEC addresses and counts from guest code and
indexed the microcode vector at address*3 with no bounds check. Invalid
shader addresses must fail before reading memory; no legitimate instruction
is modified. NativeX catches this controlled C++ exception as a failed shader.
"""
from pathlib import Path
import sys

FILE="src/graphics/pipeline/shader/translator.cpp"
INCLUDE_OLD = "#include <string>\n"
INCLUDE_NEW = "#include <stdexcept>\n#include <string>\n"
ANCHOR = """  instr.Disassemble(&ucode_disasm_buffer);
  uint32_t sequence = instr.sequence;"""
GUARD = """  // HULK/CF-BOUNDS: Xenos EXEC addresses index 3-dword instructions.
  // Malformed IM_LOAD or recycled PM4 tails can point past microcode.
  // Validate with subtraction before indexing to avoid integer overflow.
  const size_t available_instructions = ucode_data_.size() / 3;
  if (instr.instruction_address > available_instructions ||
      instr.instruction_count > available_instructions - instr.instruction_address) {
    throw std::out_of_range(
        "Xenos shader EXEC references microcode beyond declared IM_LOAD length");
  }
"""
REPLACEMENT = GUARD + ANCHOR

def patch_text(source: str) -> tuple[str, bool]:
    ready = INCLUDE_NEW in source and REPLACEMENT in source
    if ready:
        return source, False
    if "HULK/CF-BOUNDS" in source or REPLACEMENT in source or INCLUDE_NEW in source:
        raise ValueError("Partial Hulk Xenos CF bounds guard")
    if source.count(INCLUDE_OLD) != 1 or source.count(ANCHOR) != 1:
        raise ValueError("Xenos shader SDK translator EXEC anchors changed")
    if "void Shader::GatherExecInformation(" not in source:
        raise ValueError("Guard must be in SDK shader EXEC instruction analysis")
    return (source.replace(INCLUDE_OLD, INCLUDE_NEW, 1)
                  .replace(ANCHOR, REPLACEMENT, 1), True)

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_sdk_hulk_cf_bounds.py SDK_ROOT")
    path=Path(sys.argv[1])/FILE
    source=path.read_text(encoding="utf-8-sig")
    result,changed=patch_text(source)
    if changed:
        path.write_text(result,encoding="utf-8",newline="\n")
    print("Hulk SDK Xenos shader EXEC bounds:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
