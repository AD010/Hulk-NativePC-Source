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

# The optional CF memory-export successor traversal previously indexed an
# arbitrary guest control-flow address before checking whether its target was
# one of the shader's decoded CF instructions. Never dereference a bogus
# successor or wrap its index. Valid original CF control flow is unchanged.
CF_SUCCESSOR_OLD = """          uint32_t successor_cf_index = successor_stack.back();
          successor_stack.pop_back();

          ControlFlowMemExportInfo& successor_memexport_info ="""
CF_SUCCESSOR_GUARD = """          uint32_t successor_cf_index = successor_stack.back();
          successor_stack.pop_back();
          // HULK/CF-SUCCESSOR-BOUNDS: guest shader CF jump targets may be
          // invalid. The old walker dereferenced this index before checking.
          // Reject unknown targets; never fabricate a replacement branch.
          if (successor_cf_index >= cf_memexport_info_.size() ||
              (successor_cf_index >> 1) >= cf_pair_index_bound_) {
            throw std::out_of_range(
                "Xenos shader CF successor outside decoded control flow");
          }

          ControlFlowMemExportInfo& successor_memexport_info ="""

def patch_text(source: str) -> tuple[str, bool]:
    ready = (INCLUDE_NEW in source and REPLACEMENT in source and
             CF_SUCCESSOR_GUARD in source)
    if ready:
        return source, False
    if ("HULK/CF-BOUNDS" in source or "HULK/CF-SUCCESSOR-BOUNDS" in source or
            REPLACEMENT in source or CF_SUCCESSOR_GUARD in source or
            INCLUDE_NEW in source):
        raise ValueError("Partial Hulk Xenos CF bounds guard")
    if (source.count(INCLUDE_OLD) != 1 or source.count(ANCHOR) != 1 or
            source.count(CF_SUCCESSOR_OLD) != 1):
        raise ValueError("Xenos shader SDK EXEC/successor analysis anchors changed")
    if ("void Shader::GatherExecInformation(" not in source or
            "if (!cf_memexport_info_.empty()) {" not in source):
        raise ValueError("Guards must be in SDK shader EXEC and CF successor analysis")
    return (source.replace(INCLUDE_OLD, INCLUDE_NEW, 1)
                  .replace(ANCHOR, REPLACEMENT, 1)
                  .replace(CF_SUCCESSOR_OLD, CF_SUCCESSOR_GUARD, 1)), True

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
