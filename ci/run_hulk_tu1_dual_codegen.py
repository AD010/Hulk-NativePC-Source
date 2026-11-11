#!/usr/bin/env python3
"""Run authenticated EU base/TU1/optional USA ReXGlue codegen in isolation.

PRIVATE developer research only. Report contains metadata, not guest PPC.
No invocation is added to the public release workflow.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

from prepare_hulk_tu1_stage import stage
from diff_hulk_tu1_guest_ppc import check_usa_executable, read_recompiled_functions, compare


def manifest_text() -> str:
    # Never use USA-only PPC function hints with a European XEX.
    return ('[project]\nname = "hulk_eu_analysis"\n'
            'sdk_version = "0.10.0"\ngame_root = "game"\n\n'
            '[entrypoint]\nfile_path = "game/default.xex"\n'
            'out_directory_path = "generated/default"\nincludes = []\n')


def prepare_codegen(root: Path, xex: Path, xexp: Path | None = None) -> Path:
    if root.exists():
        raise FileExistsError(f"Refusing to reuse private codegen root: {root}")
    root.mkdir(parents=True)
    game = root / "game"
    game.mkdir()
    shutil.copyfile(xex, game / "default.xex")
    if xexp is not None:
        shutil.copyfile(xexp, game / "default.xexp")
    manifest = root / "analysis_manifest.toml"
    manifest.write_text(manifest_text(), encoding="utf-8")
    return manifest


def codegen(rexglue: Path, manifest: Path) -> Path:
    root = manifest.parent
    # No raw PPC/SDK diagnostics are ever printed in a PUBLIC Actions log.
    with (root / "rexglue-codegen.private.log").open("wb") as log:
        try:
            proc = subprocess.run([str(rexglue), "codegen", str(manifest)],
                                  cwd=root, stdout=log, stderr=subprocess.STDOUT,
                                  check=False)
        except OSError as error:
            raise RuntimeError("Cannot launch ReXGlue codegen executable") from error
    if proc.returncode:
        raise RuntimeError(f"ReXGlue codegen exit {proc.returncode}; see PRIVATE log")
    generated = root / "generated/default"
    if not any(generated.glob("*_recomp.*.cpp")):
        raise RuntimeError("ReXGlue produced no PPC functions; see PRIVATE log")
    return generated


def run(rexglue: Path, eu_xex: Path, tu_zip: Path, work: Path,
        report: Path, us_xex: Path | None = None) -> dict:
    if work.exists() or report.exists():
        raise FileExistsError("Refusing to overwrite existing guest-codegen results")
    if not rexglue.is_file():
        raise FileNotFoundError("ReXGlue executable absent")
    if us_xex:
        check_usa_executable(us_xex.read_bytes())

    work.mkdir(parents=True)
    staged = stage(tu_zip, eu_xex, work / "verified-tu1")
    original = staged / "codegen-game/default.xex"
    patch = staged / "codegen-game/default.xexp"

    original_manifest = prepare_codegen(work / "eu-base", original)
    patched_manifest = prepare_codegen(work / "eu-tu1", original, patch)
    usa_manifest = prepare_codegen(work / "usa-base", us_xex) if us_xex else None
    assert not (original_manifest.parent / "game/default.xexp").exists()
    assert (patched_manifest.parent / "game/default.xexp").is_file()

    base_generated = codegen(rexglue, original_manifest)
    updated_generated = codegen(rexglue, patched_manifest)
    if usa_manifest:
        codegen(rexglue, usa_manifest)

    before, _ = read_recompiled_functions(base_generated)
    after, _ = read_recompiled_functions(updated_generated)
    changes = compare(before, after)
    # Failed/suppressed sibling XEXP application must not yield a "successful"
    # analysis of zero modified instructions.
    if not (changes["changed_function_count"] or
            changes["added_function_addresses"] or
            changes["removed_function_addresses"]):
        raise RuntimeError("EU original and TU1 codegen identical: XEXP not proved applied")

    comparator = Path(__file__).resolve().with_name("diff_hulk_tu1_guest_ppc.py")
    command = [sys.executable, str(comparator),
               "--eu-xex", str(eu_xex), "--xexp", str(patch),
               "--eu-base-generated", str(base_generated),
               "--eu-tu1-generated", str(updated_generated),
               "--output", str(report)]
    if usa_manifest:
        command += ["--us-xex", str(us_xex),
                    "--us-generated", str(usa_manifest.parent / "generated/default")]
    proc = subprocess.run(command, capture_output=True, text=True, check=False)
    if proc.returncode:
        raise RuntimeError("Authenticated TU1 PPC comparator failed; private inputs retained")
    evidence = json.loads(report.read_text(encoding="utf-8"))
    if (evidence["comparison"] != changes or
            evidence["water_fix_identified"] is not False or
            evidence["us_code_patched"] is not False):
        report.unlink(missing_ok=True)
        raise RuntimeError("Invalid guest PPC evidence report")
    return evidence


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rexglue", type=Path, required=True)
    parser.add_argument("--eu-xex", type=Path, required=True)
    parser.add_argument("--tu-zip", type=Path, required=True)
    parser.add_argument("--us-xex", type=Path)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    evidence = run(args.rexglue.resolve(), args.eu_xex.resolve(),
                   args.tu_zip.resolve(), args.work.resolve(), args.report.resolve(),
                   args.us_xex.resolve() if args.us_xex else None)
    print("[HULK/TU1] changed EU functions:",
          evidence["comparison"]["changed_function_count"],
          "| water fix UNVERIFIED | USA gameplay code UNCHANGED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
