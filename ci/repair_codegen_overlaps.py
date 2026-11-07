#!/usr/bin/env python3
"""Repair *identified* ReXGlue PPC function-boundary collisions.

Keep every discovered branch target, and shorten only independently inferred
ranges: pointer scans, data-pointer tables, or PPC lis/addi references. Never
modify manual/unknown ranges. The operation must change the input, or fail.
"""
from __future__ import annotations
import pathlib
import re
import sys

OVERLAP = re.compile(
    r"\[config\] Overlapping boundaries:\s*"
    r"0x([0-9a-fA-F]{8})\+0x([0-9a-fA-F]+)\s+"
    r"overlaps\s+0x([0-9a-fA-F]{8})\+0x([0-9a-fA-F]+)"
)
START = re.compile(r"(?m)^\s*0x([0-9a-fA-F]{8})\s*=\s*\{\s*\}\s*(?:#.*)?$")
INFERRED_RANGE = re.compile(
    r"(?m)^(?P<pre>\s*0x(?P<start>[0-9a-fA-F]{8})\s*=\s*\{\s*end\s*=\s*0x)"
    r"(?P<end>[0-9a-fA-F]{8})"
    r"(?P<post>\s*\}\s*#\s*(?:pointer scan|data ptr @0x[0-9a-fA-F]+|lis/addi @0x[0-9a-fA-F]+)[^\r\n]*)$"
)

def repair(log: str, missing: str, indirect: str):
    collisions = OVERLAP.findall(log)
    if not collisions:
        raise ValueError("No ReXGlue overlap diagnostics")
    starts = {int(x, 16) for x in START.findall(missing)}
    ranges = {int(m["start"], 16): int(m["end"], 16)
              for m in INFERRED_RANGE.finditer(indirect)}
    cuts: dict[int, int] = {}
    for a_hex, alen_hex, b_hex, blen_hex in collisions:
        a, alen, b, blen = (
            int(a_hex, 16), int(alen_hex, 16),
            int(b_hex, 16), int(blen_hex, 16))
        if alen and blen == 0 and a < b < a + alen:
            outer, inner, end = a, b, a + alen
        elif blen and alen == 0 and b < a < b + blen:
            outer, inner, end = b, a, b + blen
        else:
            raise ValueError(f"Ambiguous overlap 0x{a:08X}+0x{alen:X} vs 0x{b:08X}+0x{blen:X}")
        if inner not in starts:
            raise ValueError(f"New target 0x{inner:08X} not present in missing_funcs.toml")
        if ranges.get(outer) != end:
            raise ValueError(f"0x{outer:08X} is not an identical inferred range")
        # Multiple new starts in one old range: split at the earliest. The
        # following pass must independently resolve the remaining boundaries.
        cuts[outer] = min(inner, cuts.get(outer, inner))
    def replace(m: re.Match[str]) -> str:
        start = int(m["start"], 16)
        if start not in cuts:
            return m.group(0)
        return m["pre"] + f"{cuts[start]:08X}" + m["post"]
    result = INFERRED_RANGE.sub(replace, indirect)
    if result == indirect:
        raise ValueError("No inferred range changed; refusing repeated codegen")
    return result, cuts

def main() -> int:
    if len(sys.argv) != 4:
        print("Usage: repair_codegen_overlaps.py LOG missing_funcs.toml indirect_funcs.toml",
              file=sys.stderr)
        return 2
    log, missing, indirect = map(pathlib.Path, sys.argv[1:])
    try:
        result, cuts = repair(log.read_text(errors="replace"),
                              missing.read_text(encoding="utf-8-sig"),
                              indirect.read_text(encoding="utf-8-sig"))
        indirect.write_text(result, encoding="utf-8", newline="\n")
    except (OSError, ValueError) as exc:
        print("Cannot safely repair PPC boundaries:", exc, file=sys.stderr)
        return 1
    for outer, target in sorted(cuts.items()):
        print(f"Split inferred range 0x{outer:08X} at 0x{target:08X}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
