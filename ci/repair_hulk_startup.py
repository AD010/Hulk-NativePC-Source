#!/usr/bin/env python3
"""Prepare an observed PPC candidate or verify real ReXGlue code emission.

0x824C6158 was observed in the *old* runtime logs. This is not evidence
that PAL and USA code at this address is identical. New codegen must use
the current input XEX. No stub or address relocation is invented.
"""
import argparse
import hashlib
import json
import pathlib
import re
import sys

TARGET = 0x824C6158
HEX = f"{TARGET:08X}"
START = re.compile(r"(?m)^\s*0x([0-9a-f]{8})\s*=\s*\{", re.I)
RANGE = re.compile(r"(?m)^\s*0x([0-9a-f]{8})\s*=\s*\{\s*end\s*=\s*0x([0-9a-f]{8})", re.I)

def prepare(xex, missing, indirect):
    with xex.open("rb") as f:
        if f.read(4) != b"XEX2":
            raise ValueError("Input is not an XEX2 executable")
        f.seek(0)
        sha = hashlib.file_digest(f, "sha256").hexdigest().upper()
    m = missing.read_text(encoding="utf-8-sig")
    i = indirect.read_text(encoding="utf-8-sig")
    if "[functions]" not in m or "[functions]" not in i:
        raise ValueError("Invalid [functions] TOML manifest")
    for a, b in RANGE.findall(i):
        low, high = int(a, 16), int(b, 16)
        if low < TARGET < high:
            raise ValueError(f"0x{TARGET:08X} lies within inferred PPC range "
                             f"0x{low:08X}-0x{high:08X}; must validate boundary")
    known = {int(a, 16) for a in START.findall(m)}
    known.update(int(a, 16) for a in START.findall(i))
    if TARGET not in known:
        missing.write_text(m.rstrip("\r\n") +
            f"\n0x{TARGET:08X} = {{}}  # observed in old runtime; verify USA body\n",
            encoding="utf-8", newline="\n")
        print(f"Added observed PPC entry candidate 0x{TARGET:08X}")
    else:
        print(f"Observed PPC entry 0x{TARGET:08X} already in input")
    print("HULK INPUT XEX SHA256:", sha)
    print("REGION NOTE: prior PAL/USA address equivalence is not established.")

def verify(root):
    partition = root / "codegen.partition.json"
    init = root / "incredible_hulk_init.cpp"
    register = root / "incredible_hulk_register.cpp"
    for f in (partition, init, register):
        if not f.is_file():
            raise ValueError(f"Missing generated output {f}")
    assignments = json.loads(partition.read_text(encoding="utf-8-sig")).get("assignments", {})
    if HEX not in {str(x).upper() for x in assignments}:
        raise ValueError(f"0x{HEX} absent from generated PPC partitions")
    init_txt = init.read_text(encoding="utf-8-sig")
    reg_txt = register.read_text(encoding="utf-8-sig")
    # Never assume the symbol is sub_...; imported or XDK functions may have a name.
    # The emitted initializer and dispatcher must point to the same actual function.
    generic_map = re.compile(r"\{\s*0x" + HEX +
                             r"\s*,\s*([A-Za-z_]\w*)\s*\}", re.I)
    mapping = generic_map.search(init_txt)
    if not mapping:
        context = [line.strip() for line in init_txt.splitlines() if HEX in line.upper()]
        raise ValueError(f"0x{HEX} no PPC mapping; matching init lines: {context[:8]}")
    symbol = mapping.group(1)
    expected_register = re.compile(r"SetFunction\s*\(\s*0x" + HEX +
                                   r"\s*,\s*" + re.escape(symbol) + r"\s*\)", re.I)
    if not expected_register.search(reg_txt):
        context = [line.strip() for line in reg_txt.splitlines() if HEX in line.upper()]
        raise ValueError(f"0x{HEX} dispatch mismatch for {symbol}; matching lines: {context[:8]}")
    pattern = re.compile(r"DEFINE_REX_FUNC\s*\(\s*" +
                         re.escape(symbol) + r"\s*\)\s*\{", re.I)
    candidates = []
    for cpp in root.glob("*_recomp.*.cpp"):
        source = cpp.read_text(encoding="utf-8-sig", errors="replace")
        for match in pattern.finditer(source):
            next_func = re.search(r"DEFINE_REX_FUNC\s*\(", source[match.end():])
            end = match.end() + next_func.start() if next_func else len(source)
            body = source[match.end():end]
            preamble = source[max(0, match.start()-150):match.start()]
            if "STUB: Function at" in preamble:
                continue
            compact = re.sub(r"(?m)//[^\n]*", "", body)
            compact = compact.replace("REX_FUNC_PROLOGUE();", "").strip(" \t\r\n{};")
            if compact:
                candidates.append(str(cpp))
    if not candidates:
        raise ValueError(f"0x{HEX} has no non-stub generated PPC body")
    print(f"PASS: PPC 0x{HEX} mapped, registered, and non-stub body in {candidates[0]}")
    print("LIMITATION: PPC function identity and runtime correctness require an in-game test.")

def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="action", required=True)
    a = sub.add_parser("prepare")
    a.add_argument("xex", type=pathlib.Path)
    a.add_argument("missing", type=pathlib.Path)
    a.add_argument("indirect", type=pathlib.Path)
    b = sub.add_parser("verify")
    b.add_argument("generated", type=pathlib.Path)
    args = parser.parse_args()
    try:
        if args.action == "prepare":
            prepare(args.xex, args.missing, args.indirect)
        else:
            verify(args.generated)
    except (ValueError, OSError, json.JSONDecodeError) as e:
        print("HULK PPC CHECK FAILED:", str(e), file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
