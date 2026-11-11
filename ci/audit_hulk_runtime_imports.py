#!/usr/bin/env python3
"""Audit compiled Hulk Windows import dependencies; no game files or hooks.

Uses dumpbin.exe /dependents from the MSVC developer environment, not a
runtime DLL injector. Reads EXE/DLL import names only; emits a small JSON
report for private CI diagnostics. Fail-closed strict mode is optional.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

DLL_LINE = re.compile(r"^\s*([A-Za-z0-9_.+\-]+\.dll)\s*$", re.IGNORECASE)
# Windows 10/11 OS components, not app-local redistribution candidates.
SYSTEM_DLLS = frozenset({
    "advapi32.dll", "bcrypt.dll", "cfgmgr32.dll", "combase.dll",
    "comctl32.dll", "comdlg32.dll", "crypt32.dll", "d3d11.dll",
    "d3d12.dll", "d3dcompiler_47.dll", "dbghelp.dll", "dwmapi.dll",
    "dxgi.dll", "gdi32.dll", "imm32.dll", "kernel32.dll",
    "kernelbase.dll", "mf.dll", "mfplat.dll", "msvcrt.dll",
    "ntdll.dll", "ole32.dll", "oleaut32.dll", "powrprof.dll",
    "psapi.dll", "rpcrt4.dll", "sechost.dll", "secur32.dll",
    "setupapi.dll", "shell32.dll", "shcore.dll", "shlwapi.dll",
    "uxtheme.dll", "ucrtbase.dll", "user32.dll", "userenv.dll",
    "version.dll", "winhttp.dll", "wininet.dll", "winmm.dll",
    "ws2_32.dll", "xinput1_4.dll",
})
VC_RUNTIME = re.compile(
    r"^(?:msvcp(?:140(?:_\d+)?|\d+)|vcruntime(?:140(?:_\d+)?)|"
    r"concrt(?:140)?|vcomp(?:140)?|mfc140(?:[a-z])?|"
    r"api-ms-win-crt-[a-z0-9-]+)\.dll$", re.IGNORECASE,
)


def parse_dependents(output: str) -> list[str]:
    """Collect DLL names from dumpbin /dependents, including delay-load groups."""
    names = set()
    for line in output.splitlines():
        match = DLL_LINE.fullmatch(line)
        if match:
            names.add(match.group(1).lower())
    return sorted(names)


def classify(name: str, bundled: set[str]) -> str:
    name = name.lower()
    if name in bundled:
        return "app_local"
    if name.startswith(("api-ms-win-", "ext-ms-win-")):
        return "windows_api_set"
    if name in SYSTEM_DLLS:
        return "windows_system"
    if VC_RUNTIME.fullmatch(name):
        return "system_vc_redist"
    if name == "vulkan-1.dll":
        return "vulkan_loader_driver"
    return "unresolved_external"


def audit_directory(folder: Path, *, dumpbin: str = "dumpbin") -> dict:
    binaries = sorted((p for p in folder.iterdir() if p.is_file() and
                       p.suffix.lower() in (".dll", ".exe")),
                      key=lambda p: p.name.lower())
    if not binaries:
        raise ValueError("No EXE or DLL files found in release directory")
    bundled = {p.name.lower() for p in binaries if p.suffix.lower() == ".dll"}
    entries = {}
    unresolved = set()
    vc = set()
    for binary in binaries:
        process = subprocess.run(
            [dumpbin, "/dependents", str(binary)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            errors="replace", check=False,
        )
        if process.returncode:
            raise RuntimeError(
                f"dumpbin failed for {binary.name} with code {process.returncode}; "
                "check MSVC developer-command environment")
        imports = parse_dependents(process.stdout)
        if not imports:
            raise ValueError(f"No import dependencies found in {binary.name}")
        entries[binary.name] = {dll: classify(dll, bundled) for dll in imports}
        unresolved.update(dll for dll in imports if classify(dll, bundled) == "unresolved_external")
        vc.update(dll for dll in imports if classify(dll, bundled) == "system_vc_redist")
    return {
        "binaries": entries,
        "requires_system_vc_redist": sorted(vc),
        "unresolved_external": sorted(unresolved),
        "interpretation": (
            "Import/deployment audit only; does not determine Vulkan, PM4, "
            "shader, rendering or gameplay correctness. Vulkan loader is "
            "supplied by the graphics driver, not copied from another game."
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--dumpbin", default="dumpbin")
    parser.add_argument("--strict", action="store_true",
                        help="Fail if any non-system DLL is not bundled")
    args = parser.parse_args()
    if shutil.which(args.dumpbin) is None:
        parser.error("MSVC dumpbin.exe unavailable; run in VS developer environment")
    report = audit_directory(args.directory, dumpbin=args.dumpbin)
    rendered = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print("[HULK/PE-IMPORTS] VC runtimes:", ", ".join(report["requires_system_vc_redist"]) or "none")
    print("[HULK/PE-IMPORTS] Unresolved external DLLs:",
          ", ".join(report["unresolved_external"]) or "none")
    return 2 if args.strict and report["unresolved_external"] else 0


if __name__ == "__main__":
    sys.exit(main())
