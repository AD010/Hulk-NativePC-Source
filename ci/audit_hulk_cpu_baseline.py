#!/usr/bin/env python3
"""Reject an accidental mandatory AVX baseline in Hulk/SDK/NativeX/Diligent.
Compiler command audit, not a proof of arbitrary inline asm or binary dispatch.
Architecture-specific third-party sources (e.g. FFmpeg) can be dispatched at
runtime and are deliberately excluded, but must be audited separately.
"""
import argparse
import json
import re
from pathlib import Path

BASELINE = "-march=x86-64-v2 -mno-avx -mno-avx2"
ALLOWED_ARCH = {"x86-64", "x86-64-v2", "x86-64-v1"}
UNSAFE = re.compile(r"(?<![\w-])(?:-m(?:avx[\w.]*|fma4?|f16c|xop|bmi2?|amx[\w-]*)|[/\-]arch:AVX[\w.-]*)(?=[\s\"']|$)", re.I)
MARCH = re.compile(r"(?:^|[\s\"'])-march=([^\s\"']+)")
THIRD_PARTY = re.compile(r"/(?:thirdparty|third_party|extern|external|_deps|deps|vendor)/", re.I)

def audit(entries, root):
    root = root.replace("\\", "/").rstrip("/").lower() + "/"
    failures = []
    audited = 0
    for entry in entries:
        file = entry["file"].replace("\\", "/").lower()
        if not file.startswith(root):
            continue
        rel = file[len(root):]
        # Include DiligentCore itself even though NativeX vendors it.
        if THIRD_PARTY.search("/" + rel) and "/diligent-core/" not in "/" + rel:
            continue
        if not rel.endswith((".c", ".cc", ".cpp", ".cxx")):
            continue
        command = " ".join(entry.get("arguments", [])) or entry.get("command", "")
        audited += 1
        bad = UNSAFE.search(command)
        arch = [x for x in MARCH.findall(command) if x not in ALLOWED_ARCH]
        if bad or arch:
            # Do not print paths, generated guest code or full command lines.
            failures.append(bad.group(0) if bad else "-march=" + arch[0])
    if not audited:
        raise ValueError("No first-party C/C++ compilation commands inspected")
    return audited, failures

def main():
    p = argparse.ArgumentParser()
    p.add_argument("database", type=Path)
    p.add_argument("root", type=Path)
    a = p.parse_args()
    n, failures = audit(json.loads(a.database.read_text(encoding="utf-8")), str(a.root.resolve()))
    print(f"[HULK/CPU] inspected {n} C/C++ compilation commands, incompatible={len(failures)}")
    if failures:
        raise SystemExit("Mandatory AVX-class architecture flags in first-party build: " +
                         ", ".join(sorted(set(failures))))

if __name__ == "__main__":
    main()
