#!/usr/bin/env python3
"""Build genuine Hulk-patched XenosRecomp and compile REAL live EoR shaders.

This does NOT start a Windows Release, add a workflow, or publish game data.
A single invocation preserves already patched source, existing HEORSPV1
checkpoints, and private source archives. --watch keeps compiling only NEW
original ShaderEffects passes observed by the live XDK render worker.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

PINNED_XENOSRECOMP = "70a084c2392378818a4da4086e8e3420b4c3ea8d"
XENOS_REPO = "https://github.com/hedge-dev/XenosRecomp.git"


def run(command: list[str], cwd: Path | None = None) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def git_stdout(path: Path, *args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=path, text=True).strip()


def ensure_checkout(work: Path) -> Path:
    checkout = work / "XenosRecomp"
    if not (checkout / ".git").exists():
        if checkout.exists():
            raise ValueError("Private XenosRecomp checkout exists without Git metadata")
        work.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--recursive", XENOS_REPO, str(checkout)])
    # A previously installed Hulk patch must never be thrown away. Its
    # source commit is still pinned; only the worktree is modified.
    if git_stdout(checkout, "rev-parse", "HEAD") != PINNED_XENOSRECOMP:
        if git_stdout(checkout, "status", "--porcelain"):
            raise ValueError("Existing XenosRecomp has local edits: refusing reset")
        run(["git", "fetch", "origin", PINNED_XENOSRECOMP], cwd=checkout)
        run(["git", "checkout", "--detach", PINNED_XENOSRECOMP], cwd=checkout)
    run(["git", "submodule", "update", "--init", "--recursive"], cwd=checkout)
    return checkout


def install_original_xenos_adapters(checkout: Path, repository: Path) -> None:
    # The 4th source/operand patch is installed by the gradients patch.
    # These scripts are idempotent and verify their original XenosRecomp
    # ABI before changing anything; unsupported fetches fail closed.
    for name in (
        "patch_xenosrecomp_hulk_fetch.py",
        "patch_xenosrecomp_hulk_lod.py",
        "patch_xenosrecomp_hulk_gradients.py",
    ):
        run([sys.executable, str(repository / "ci" / name), str(checkout)])


def compiler_paths(checkout: Path, build: Path) -> tuple[Path, Path, Path]:
    suffix = ".exe" if os.name == "nt" else ""
    xenos = build / "XenosRecomp" / ("XenosRecomp" + suffix)
    if not xenos.is_file():
        matches = list(build.rglob("XenosRecomp" + suffix))
        matches = [p for p in matches if p.is_file()]
        if len(matches) != 1:
            raise FileNotFoundError(
                "No unique compiled, Hulk-patched XenosRecomp executable")
        xenos = matches[0]
    arch = "x64"
    dxc = (checkout / "thirdparty" / "dxc-bin" / "bin" / arch /
           ("dxc.exe" if os.name == "nt" else "dxc-linux"))
    common = checkout / "XenosRecomp" / "shader_common.h"
    if not dxc.is_file() or not common.is_file():
        raise FileNotFoundError("Pinned XenosRecomp DXC/compiler header unavailable")
    return xenos, dxc, common


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--private-bank", required=True, type=Path,
                   help="User-owned existing original 5194-program Xenos bank")
    p.add_argument("--private-work", required=True, type=Path)
    p.add_argument("--private-install", required=True, type=Path,
                   help="Same eor_offline folder used by the live Vulkan worker")
    p.add_argument("--observed-passes", required=True, type=Path,
                   help="Live guest HEORREQ1.txt from the existing worker")
    p.add_argument("--repository", type=Path,
                   default=Path(__file__).resolve().parents[1])
    p.add_argument("--workers", type=int, default=4)
    p.add_argument("--first-passes", type=int, default=0,
                   help="First N observed EoR ShaderEffects passes, 0=all")
    p.add_argument("--watch", action="store_true",
                   help="Compile newly observed real Xbox passes continuously")
    args = p.parse_args()
    repo = args.repository.resolve()
    work = args.private_work.resolve()
    original = args.private_bank.resolve()
    install = args.private_install.resolve()
    observed = args.observed_passes.resolve()
    for private in (work, original, install, observed):
        if private == repo or repo in private.parents:
            p.error("Original game sources and AOT products must stay OUTSIDE Git")
    if not (original / "HEORMAT2.bin").is_file():
        p.error("Original private HEORMAT2 ShaderEffects source ledger not found")
    if not shutil.which("cmake") or not shutil.which("git"):
        p.error("CMake and Git are required to bootstrap the original shader translator")
    checkout = ensure_checkout(work)
    install_original_xenos_adapters(checkout, repo)
    build = work / "xenosrecomp_hulk_build"
    run(["cmake", "-S", str(checkout), "-B", str(build),
         "-DCMAKE_BUILD_TYPE=Release", "-DXENOS_RECOMP_DXIL=OFF"])
    run(["cmake", "--build", str(build), "--config", "Release",
         "--parallel", str(max(1, min(os.cpu_count() or 2, 8)))])
    xenos, dxc, common = compiler_paths(checkout, build)
    if os.name != "nt":
        dxc.chmod(dxc.stat().st_mode | 0o111)
    command = [
        sys.executable, str(repo / "ci" / "hulk_eor_first_present_aot.py"),
        "--bank", str(original),
        "--work", str(work / "aot"),
        "--install", str(install),
        "--public-repository", str(repo),
        "--observed-passes", str(observed),
        "--xenosrecomp", str(xenos),
        "--dxc", str(dxc),
        "--common", str(common),
        "--workers", str(args.workers),
        "--max-observed-passes", str(args.first_passes),
    ]
    if args.watch:
        command.append("--watch")
    run(command)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
