"""Finds functions the game reaches only through pointers, which codegen's analysis does not find.

A port of King Kong's tools/FindMissingFuncs.cs. Without these the game stops with
"Call to invalid or unregistered function at guest address 0x...".

It looks at code addresses held in data (vtables, callback tables) and built with lis/addi or lis/ori pairs in
code, and keeps those that are not a known function or a jump table label and follow a blr, bctr, b or padding.
Each becomes a [functions] entry running to the next known function or the first padding word.

Inputs: the loaded game image (start the game once with HULK_DUMP_IMAGE=runs/image.bin), and the last codegen
output in generated/default. Writes indirect_funcs.toml; run codegen again afterwards.

Usage: python tools/find_missing_funcs.py runs/image.bin [text_start text_end]
       (the code range is in the game log: "Function table initialized for module: code=82110000-82E0985B")
With runs/codegen_debug.log (codegen run with --log-level debug) an address inside a function the analysis already
found is left out: after a blr in the middle of a function the next word looks like a start, and splitting the
function there breaks it (19 such addresses stopped the game at start).
"""
import bisect
import os
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "generated" / "default"
OUT = ROOT / "indirect_funcs.toml"
IMAGE_BASE = 0x82000000


def main():
    img = Path(sys.argv[1]).read_bytes()
    text_start = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x82110000
    text_end = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x82E0985C
    words = struct.unpack(f">{len(img) // 4}I", img[: len(img) // 4 * 4])

    def w(addr):
        return words[(addr - IMAGE_BASE) >> 2]

    known = {int(a, 16) for a in re.findall(r'"([0-9A-F]{8})"\s*:', (GEN / "codegen.partition.json").read_text())}
    labels = set()
    for f in GEN.glob("*_recomp.*.cpp"):
        labels.update(int(a, 16) for a in re.findall(r"^loc_([0-9A-F]{8}):", f.read_text(errors="replace"), re.M))
    # Import thunks (resolved by the runtime) are one block of 16 byte stubs; leave them out. In King Kong they
    # end the code, in this game they sit inside it.
    init = (GEN / "incredible_hulk_init.cpp").read_text(errors="replace")
    imports = [int(a, 16) for a in re.findall(r"\{ 0x([0-9A-F]{8}), __imp__", init)]
    thunk_start, thunk_end = (min(imports), max(imports) + 16) if imports else (text_end, text_end)

    def in_text(a):
        return text_start <= a < text_end and not thunk_start <= a < thunk_end and a % 4 == 0

    def terminator(x):
        return x in (0x4E800020, 0x4E800420) or ((x >> 26) == 18 and (x & 3) == 0) or x == 0

    refs = {}
    # 1. Code addresses held in data.
    for i, v in enumerate(words):
        a = IMAGE_BASE + i * 4
        if text_start <= a < text_end:
            continue
        if in_text(v):
            refs.setdefault(v, f"data ptr @0x{a:08X}")
    # 2. lis rX,hi then addi/ori rY,rX,lo in code.
    lis = [None] * 32
    for a in range(text_start, text_end - 3, 4):
        x = w(a)
        op, rd, ra = x >> 26, (x >> 21) & 31, (x >> 16) & 31
        if op == 15 and ra == 0:
            lis[rd] = (x & 0xFFFF) << 16
            continue
        if op in (14, 24) and lis[ra] is not None:
            lo = x & 0xFFFF
            v = (lis[ra] + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF if op == 14 else lis[ra] | lo
            # A target just after the pair is a switch table base, not a function.
            if in_text(v) and not (v > a and v - a < 0x40):
                refs.setdefault(v, f"lis/addi @0x{a:08X}")
        if terminator(x):
            lis = [None] * 32

    explain = os.environ.get("FINDMISSING_EXPLAIN")
    if explain:
        t = int(explain, 16)
        print(f"explain {t:08X}: ref={refs.get(t)} known={t in known} label={t in labels} "
              f"prev_terminator={terminator(w(t - 4))} (prev={w(t - 4):08X}) thunks={thunk_start:08X}-{thunk_end:08X}")

    spans = {}
    debug_log = ROOT / "runs" / "codegen_debug.log"
    if debug_log.exists():
        for a, n in re.findall(r"FunctionNode 0x([0-9A-F]{8}) \(sub_[0-9A-F]+\): DISCOVERED with \d+ blocks, (\d+) instructions",
                               debug_log.read_text(errors="replace")):
            spans[int(a, 16)] = int(n) * 4
    span_starts = sorted(spans)

    def inside_known(t):
        i = bisect.bisect_right(span_starts, t) - 1
        return i >= 0 and span_starts[i] != t and t < span_starts[i] + spans[span_starts[i]]

    cands = sorted(t for t in refs if t not in known and t not in labels and terminator(w(t - 4)) and w(t) != 0
                   and not inside_known(t))
    starts = sorted(known | set(cands))
    # Entries already in the file are kept: after codegen they count as known, so a new scan no longer lists them.
    lines = {}
    if OUT.exists():
        for a, rest in re.findall(r"^0x([0-9A-F]{8}) = (.*)$", OUT.read_text(), re.M):
            lines[int(a, 16)] = f"0x{a} = {rest}\n"
    for c in cands:
        i = bisect.bisect_right(starts, c)
        nxt = starts[i] if i < len(starts) else text_end
        end = c
        while end < nxt and w(end) != 0:
            end += 4
        lines[c] = f"0x{c:08X} = {{ end = 0x{end:08X} }}  # {refs[c]}\n"
    OUT.write_text("# Functions reached only through pointers. Made by tools/find_missing_funcs.py.\n[functions]\n"
                   + "".join(lines[k] for k in sorted(lines)), newline="\n")
    print(f"known={len(known)} labels={len(labels)} code_refs={len(refs)} missing={len(cands)}")


if __name__ == "__main__":
    main()
