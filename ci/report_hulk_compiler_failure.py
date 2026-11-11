#!/usr/bin/env python3
"""Emit bounded, sanitized compiler failures from a PRIVATE Hulk build log.

Only diagnostic categories and public/SDK *source filenames* are printed.
Never print PPC instructions, generated guest code, XEX bytes or arbitrary
log lines. Designed for the already-existing public Windows workflow.
"""
from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path
import re

ERROR = re.compile(
    r"(?i)\b(?:fatal error|error C\d{4}|error LNK\d{4}|"
    r"error MSB\d{4}|error:|CMake Error|ValueError:|"
    r"AssertionError:|ModuleNotFoundError:|"
    r"ninja: error|undefined reference)\b"
)
MSVC = re.compile(r"(?i)\b(?:fatal )?error (C\d{4}|LNK\d{4}|MSB\d{4})\b")
STAGE = re.compile(r"(?i)(?:^|\s)RUN:\s*(.*)")
SOURCE = re.compile(
    r"(?i)([\w.-]+\.(?:cpp|c|h|hpp|py|cmake|ps1))"
)
EXTRA = re.compile(
    r"(?i)\b(?:ValueError|AssertionError|ModuleNotFoundError|"
    r"CMake Error|ninja: error|undefined reference|fatal error|error:)\b"
)
# A function named sub_XXXXXXXX or a generated guest-source filename must not
# accidentally be published in CI. The standard errors keep only the class,
# compiler error code, and non-proprietary source basename.
PRIVATE = re.compile(r"(?i)(?:sub_[0-9a-f]{8}|[\w]+_recomp\.\d+\.cpp)")
SECRET = re.compile(r"(?i)(?:GH_TOKEN|GITHUB_TOKEN|password|secret|xex|xexp)")


def summarize(text: str, cap: int = 16) -> list[str]:
    lines = text.splitlines()
    matches = []
    for line in lines:
        if not ERROR.search(line):
            continue
        codes = MSVC.findall(line)
        if codes:
            category = codes[-1].upper()
        else:
            found = EXTRA.search(line)
            category = found.group(0).lower() if found else "unknown compiler failure"
        source = SOURCE.findall(line)
        safe = [name for name in source
                if not PRIVATE.search(name) and not SECRET.search(name)
                and not name.lower().endswith((".xex", ".xexp"))]
        basename = safe[0] if safe else "source-unavailable"
        matches.append(f"{category} [{basename}]")
    if not matches:
        return ["No classified compiler errors; inspect private diagnostics"]
    unique = Counter(matches)
    result = [f"{name} count={n}" for name, n in unique.most_common(cap)]
    result.append(f"classified_error_lines={len(matches)}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    # Large build logs are fine; output is capped. Not a log redistributor.
    log = args.log.read_text(encoding="utf-8-sig", errors="replace")
    for item in summarize(log):
        print("[HULK/PRIVATE-COMPILER/REDACTED]", item)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
