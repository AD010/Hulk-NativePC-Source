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
    r"ninja: error|undefined reference)"
)
MSVC = re.compile(r"(?i)\b(?:fatal )?error (C\d{4}|LNK\d{4}|MSB\d{4})\b")
STAGE = re.compile(r"(?i)(?:^|\s)RUN:\s*(.*)")
SOURCE = re.compile(
    r"(?i)([\w.-]+\.(?:cpp|c|h|hpp|py|cmake|ps1))"
)
EXTRA = re.compile(
    r"(?i)\b(?:ValueError|AssertionError|ModuleNotFoundError|"
    r"CMake Error|ninja: error|undefined reference|fatal error|error:)"
)
# A function named sub_XXXXXXXX or a generated guest-source filename must not
# accidentally be published in CI. The standard errors keep only the class,
# compiler error code, and non-proprietary source basename.
PRIVATE = re.compile(r"(?i)(?:sub_[0-9a-f]{8}|[\w]+_recomp\.\d+\.cpp)")
SECRET = re.compile(r"(?i)(?:GH_TOKEN|GITHUB_TOKEN|password|secret|xex|xexp)")



# Only well-known compiler command names and PUBLIC script basenames.
# Never copy a command's arbitrary arguments (they can contain guest paths).
KNOWN_TOOLS = ("python", "cmake", "ninja", "clang++", "clang-cl",
               "git", "rexglue.exe", "powershell.exe", "ctest")
ALLOWED_FAILURE_HINTS = {
    "unrecognized arguments": "invalid-cli-arguments",
    "invalid choice": "invalid-cli-choice",
    "no such file or directory": "missing-file-or-directory",
    "not found": "file-or-tool-not-found",
    "not recognized": "command-not-recognized",
    "unable to": "tool-unable-to",
    "failed to": "tool-reported-failure",
    "access is denied": "filesystem-access-denied",
    "permission denied": "filesystem-permission-denied",
    "unknown argument": "unknown-cli-argument",
    "unknown option": "unknown-cli-option",
    "out of memory": "out-of-memory",
    "no space left on device": "disk-full",
    "invalid syntax": "python-syntax",
    "syntaxerror": "python-syntax",
    "could not resolve": "unresolved-dependency",
}


def stage_report(log: str, cap: int = 8) -> list[str]:
    stages = []
    for line in log.splitlines():
        match = STAGE.search(line)
        if not match:
            continue
        command = match.group(1).strip()
        if not command:
            continue
        tool = next((name for name in KNOWN_TOOLS
                     if re.search(r"(?i)(?:^|[\\/\s])" + re.escape(name) +
                                  r"(?:\s|$)", command)), None)
        if not tool:
            continue
        files = SOURCE.findall(command)
        # Source only: reveal ci/python script filename when it is part of the
        # repository's own CI directory. No paths/command args are printed.
        script = next((name for name in files
                       if name.startswith(("test_", "patch_", "audit_", "repair_",
                                           "check_", "seed_", "run_", "hulk_"))
                       and name.endswith(".py") and not SECRET.search(name)), "")
        stages.append(tool + (" script=" + script if script else ""))
    if not stages:
        return ["last_commands=unavailable"]
    return ["last_commands=" + " -> ".join(stages[-cap:])]


def hint_report(log: str, cap: int = 12) -> list[str]:
    hints = []
    for line in log.splitlines():
        if not ERROR.search(line):
            continue
        lower = line.lower()
        hints.extend(name for needle, name in ALLOWED_FAILURE_HINTS.items()
                     if needle in lower)
    counts = Counter(hints)
    return ["error_hints=" + ", ".join(
        f"{name}:{count}" for name, count in counts.most_common(cap)
    )] if counts else ["error_hints=unclassified"]


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
    for item in summarize(log) + stage_report(log) + hint_report(log):
        print("[HULK/PRIVATE-COMPILER/REDACTED]", item)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
