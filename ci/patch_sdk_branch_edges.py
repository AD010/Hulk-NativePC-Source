#!/usr/bin/env python3
"""Patch the ReXGlue recomp C++ emitter to recover a known guest branch target.

When an Xbox 360 branch targets a real FunctionNode but Merge didn't attach a
CallEdge to that specific instruction, the emitter currently emits REX_FATAL and
marks codegen unsuccessful. More passes over unchanged function lists cannot
fix this. Use the already validated FunctionGraph entry directly. Do NOT turn
unknown addresses into speculative functions or silence unresolved instructions.
"""
from pathlib import Path
import sys

TARGET = Path("src/codegen/builders/context.cpp")
GLUE_TARGET = Path("src/rexglue/commands/project_scan.cpp")
GLUE_ANCHOR = "  auto names = parse_app_name(std::string(project_name));\n  nlohmann::json data ="
GLUE_REPLACE = ("  auto names = parse_app_name(std::string(project_name));\n"
                "  // Preserve the literal manifest project name, including underscores.\n"
                "  names.original = std::string(project_name);\n"
                "  nlohmann::json data =")

CALL_ANCHOR = """  // No pre-resolved target found - this is an error
  REXCODEGEN_ERROR("Unresolved function 0x{:08X} from 0x{:08X} (no CallTarget in FunctionNode)",
                   address, base);"""
CALL_REPLACE = """  // The target may be a validated FunctionNode even when the merge phase
  // omitted the CallEdge at this instruction site. Reuse that exact symbol.
  // No address guessing: missing functions and imports still fail closed.
  if (const auto* known = graph().getFunction(address); known && !known->isImport()) {
    const auto& name = known->name();
    emitCtx.reference(name);
    println("\\t{}(ctx, base);", name);
    return;
  }

  // No pre-resolved target found - this is an error
  REXCODEGEN_ERROR("Unresolved function 0x{:08X} from 0x{:08X} (no CallTarget in FunctionNode)",
                   address, base);"""

COND_ANCHOR = """      } else {
        REXCODEGEN_ERROR("Unresolved conditional branch to 0x{:08X} from 0x{:08X} (no CallTarget)",
                         target, base);"""
COND_REPLACE = """      } else if (const auto* known = graph().getFunction(target);
                 known && !known->isImport()) {
        // classifyTarget already confirmed this is a separate guest function.
        // A missing per-site CallEdge does not make that function unknown.
        emitCtx.reference(known->name());
        println("\\tif ({}{}.{}) {{", not_ ? "!" : "", cr(insn.operands[0]), cond);
        println("\\t\\t{}(ctx, base);", known->name());
        println("\\t\\treturn;");
        println("\\t}}");
      } else {
        REXCODEGEN_ERROR("Unresolved conditional branch to 0x{:08X} from 0x{:08X} (no CallTarget)",
                         target, base);"""

def apply(source: str) -> str:
    for before, after in ((CALL_ANCHOR, CALL_REPLACE), (COND_ANCHOR, COND_REPLACE)):
        if source.count(before) == 1:
            source = source.replace(before, after)
        elif source.count(after) == 1:
            # Already patched SDK from the same workflow cache.
            pass
        else:
            raise ValueError(f"unexpected upstream emitter layout near: {before[:75]!r}")
    return source

def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: patch_sdk_branch_edges.py PATH_TO_REXGLUE_SDK", file=sys.stderr)
        return 2
    file = Path(sys.argv[1]) / TARGET
    if not file.exists():
        print(f"Missing ReXGlue source: {file}", file=sys.stderr)
        return 1
    try:
        original = file.read_text(encoding="utf-8-sig")
        result = apply(original)
    except (ValueError, OSError) as exc:
        print(f"Cannot patch C++ branch emission: {exc}", file=sys.stderr)
        return 1
    glue_file = Path(sys.argv[1]) / GLUE_TARGET
    if not glue_file.exists():
        print(f"Missing ReXGlue naming source: {glue_file}", file=sys.stderr)
        return 1
    try:
        glue_original = glue_file.read_text(encoding="utf-8-sig")
        if glue_original.count(GLUE_ANCHOR) == 1:
            glue_result = glue_original.replace(GLUE_ANCHOR, GLUE_REPLACE)
        elif glue_original.count(GLUE_REPLACE) == 1:
            glue_result = glue_original
        else:
            raise ValueError("Unexpected project_scan.cpp naming layout")
    except (ValueError, OSError) as exc:
        print(f"Cannot fix generated CMake naming: {exc}", file=sys.stderr)
        return 1
    if glue_result != glue_original:
        glue_file.write_text(glue_result, encoding="utf-8", newline="\n")
        print("Fixed generated CMake manifest and target naming.")
    if original != result:
        file.write_text(result, encoding="utf-8", newline="\n")
        print("Patched ReXGlue known-function call and conditional branch emission.")
    else:
        print("ReXGlue known-function branch patch restored from SDK build cache.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
