#!/usr/bin/env python3
"""Source-verified ReXGlue v0.11.0 PPC correctness backports for Hulk's pinned recomp fork.
Upstream: c9f19382 (fctid), 02a002f8 (srad/sraw), ab61f2e6 (vcmpbfp).
No official SDK runtime/Xenos/PM4 ABI upgrade is attempted.
"""
from __future__ import annotations
from pathlib import Path
import subprocess
import sys

PINNED_RECOMP_SHA = "4348a344eb86ae456e1ccf7d393085d720cc6ce6"
FLOAT = Path("src/codegen/builders/floating_point.cpp")
LOGICAL = Path("src/codegen/builders/logical.cpp")
VECTOR = Path("src/codegen/builders/vector.cpp")

class SourceDrift(ValueError):
    pass

def replace_exact(source: str, old: str, new: str, count: int = 1) -> str:
    if source.count(old) != count:
        raise SourceDrift(f"expected {count} occurrences of {old[:90]!r}, found {source.count(old)}")
    if source.count(new):
        raise SourceDrift("partly applied backport or ambiguous upstream code")
    return source.replace(old, new)

def one_function(source: str, start: str, stop: str, edits: tuple[tuple[str,str], ...]) -> str:
    if source.count(start) != 1 or source.count(stop) != 1:
        raise SourceDrift("missing/duplicate SDK PPC builder function")
    begin, end = source.index(start), source.index(stop, source.index(start))
    body = source[begin:end]
    for old, new in edits:
        body = replace_exact(body, old, new)
    return source[:begin] + body + source[end:]

def backport_fctid(source: str) -> str:
    return replace_exact(source,
        "({1}.f64 > double(LLONG_MAX)) ? LLONG_MAX : ",
        "({1}.f64 >= 0x1p63) ? LLONG_MAX : ", 2)

def backport_shifts(source: str) -> str:
    srad_old = '  ctx.println("\\t{}.s64 = {}.s64 >> {}.u64;", ctx.r(ctx.insn.operands[0]),'
    sraw_old = '  ctx.println("\\t{}.s64 = {}.s32 >> {}.u32;", ctx.r(ctx.insn.operands[0]),'
    return one_function(one_function(source,
        "bool build_srad(BuilderContext& ctx) {", "bool build_sradi(BuilderContext& ctx) {",
        ((srad_old,
          '  // A shift of 64..127 sets XER.CA to the sign of the source.\n'
          '  ctx.println("\\tif ({}.u8 & 0x40) {}.ca = {}.s64 < 0;",\n'
          '              ctx.r(ctx.insn.operands[2]), ctx.xer(), ctx.r(ctx.insn.operands[1]));\n'
          + srad_old),)),
        "bool build_sraw(BuilderContext& ctx) {", "bool build_srawi(BuilderContext& ctx) {",
        ((sraw_old,
          '  // A shift of 32..63 sets XER.CA to the sign of the source.\n'
          '  ctx.println("\\tif ({}.u8 & 0x20) {}.ca = {}.s32 < 0;",\n'
          '              ctx.r(ctx.insn.operands[2]), ctx.xer(), ctx.r(ctx.insn.operands[1]));\n'
          + sraw_old),))

def backport_vcmpbfp(source: str) -> str:
    return one_function(source, "bool build_vcmpbfp(BuilderContext& ctx) {",
        "bool build_vcmpeqfp(BuilderContext& ctx) {", (
        ("simde_mm_and_ps(simde_mm_cmpgt_ps", "simde_mm_andnot_ps(simde_mm_cmple_ps"),
        ("simde_mm_and_ps(simde_mm_cmplt_ps", "simde_mm_andnot_ps(simde_mm_cmpge_ps"),
        ("  if (isRecordForm(ctx.insn))\n    ctx.println(",
         "  if (isRecordForm(ctx.insn)) {\n    ctx.println("),
        ("        ctx.cr(6), vD, vD);\n  return true;",
         '        ctx.cr(6), vD, vD);\n'
         '    ctx.println("\\t{}.lt = 0;", ctx.cr(6));\n'
         '  }\n  return true;'),
    ))

TRANSFORMS = {FLOAT: backport_fctid, LOGICAL: backport_shifts, VECTOR: backport_vcmpbfp}

def undo_complete(path: Path, content: str) -> str:
    if path == FLOAT:
        old = "({1}.f64 >= 0x1p63) ? LLONG_MAX : "
        if content.count(old) != 2 or "({1}.f64 > double(LLONG_MAX)) ? LLONG_MAX : " in content:
            raise SourceDrift("incomplete fctid")
        return content.replace(old, "({1}.f64 > double(LLONG_MAX)) ? LLONG_MAX : ")
    if path == LOGICAL:
        changes = [
            ('  // A shift of 64..127 sets XER.CA to the sign of the source.\n'
             '  ctx.println("\\tif ({}.u8 & 0x40) {}.ca = {}.s64 < 0;",\n'
             '              ctx.r(ctx.insn.operands[2]), ctx.xer(), ctx.r(ctx.insn.operands[1]));\n'),
            ('  // A shift of 32..63 sets XER.CA to the sign of the source.\n'
             '  ctx.println("\\tif ({}.u8 & 0x20) {}.ca = {}.s32 < 0;",\n'
             '              ctx.r(ctx.insn.operands[2]), ctx.xer(), ctx.r(ctx.insn.operands[1]));\n')
        ]
        if any(content.count(c) != 1 for c in changes):
            raise SourceDrift("incomplete srad/sraw")
        for c in changes:
            content = content.replace(c, "", 1)
        return content
    if path == VECTOR:
        return one_function(content, "bool build_vcmpbfp(BuilderContext& ctx) {",
            "bool build_vcmpeqfp(BuilderContext& ctx) {", (
            ("simde_mm_andnot_ps(simde_mm_cmple_ps", "simde_mm_and_ps(simde_mm_cmpgt_ps"),
            ("simde_mm_andnot_ps(simde_mm_cmpge_ps", "simde_mm_and_ps(simde_mm_cmplt_ps"),
            ("  if (isRecordForm(ctx.insn)) {\n    ctx.println(", "  if (isRecordForm(ctx.insn))\n    ctx.println("),
            ('        ctx.cr(6), vD, vD);\n    ctx.println("\\t{}.lt = 0;", ctx.cr(6));\n  }\n  return true;',
             "        ctx.cr(6), vD, vD);\n  return true;"),
        ))
    raise SourceDrift(f"unknown source: {path}")

def inspect_source(root: Path) -> dict[Path,str]:
    """Check all sources before writing any, reject drift/partial install."""
    outputs = {}
    for path, fn in TRANSFORMS.items():
        content = (root / path).read_text(encoding="utf-8-sig")
        try:
            outputs[path] = fn(content)
        except SourceDrift as exc:
            try:
                pristine = undo_complete(path, content)
                if fn(pristine) != content:
                    raise SourceDrift("inconsistent backport")
            except SourceDrift:
                raise SourceDrift(f"{path}: {exc}") from exc
            outputs[path] = content
    return outputs

def verify_sdk_head(root: Path) -> None:
    result = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"],
                            check=True, text=True, capture_output=True)
    if result.stdout.strip() != PINNED_RECOMP_SHA:
        raise SourceDrift(f"unreviewed SDK HEAD {result.stdout.strip()}; expected {PINNED_RECOMP_SHA}")

def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_sdk_upstream_v011_ppc.py <pinned-recomp-sdk>", file=sys.stderr)
        return 2
    root = Path(sys.argv[1])
    try:
        verify_sdk_head(root)
        outputs = inspect_source(root)
        for path, updated in outputs.items():
            current = (root / path).read_text(encoding="utf-8-sig")
            if current != updated:
                (root / path).write_text(updated, encoding="utf-8", newline="\n")
                print(f"[HULK/SDK-0.11] applied PPC correctness to {path}")
            else:
                print(f"[HULK/SDK-0.11] already applied {path}")
    except (OSError, SourceDrift, subprocess.CalledProcessError) as exc:
        print(f"[HULK/SDK-0.11] refused: {exc}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
