#!/usr/bin/env python3
"""Private Hulk XenosRecomp -> HLSL -> DXC/SPIR-V toolchain.

Uses actual SHA-256-verified containers extracted by hulk_shader_effect_bank.py.
No original game binaries, HLSL, SPIR-V or shader cache are written into Git.
XenosRecomp is game-specific: conversion success alone is not render parity.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

SHA = re.compile(r"^[0-9a-f]{64}$")
MAX_INPUT = 2 << 20
MAX_SPIRV = 8 << 20
SPIRV_MAGIC = 0x07230203


def _private(root: Path, repository: Path) -> Path:
    value = root.resolve()
    source = repository.resolve()
    if value == source or source in value.parents:
        raise ValueError("Shader source/outputs must stay outside the public repository")
    return value


def _check_spirv(data: bytes) -> None:
    if len(data) < 20 or len(data) > MAX_SPIRV or len(data) % 4:
        raise ValueError("Invalid output SPIR-V byte length")
    magic, version, generator, bound, schema = struct.unpack_from("<5I", data)
    if magic != SPIRV_MAGIC or version < 0x10000 or version > 0x10600:
        raise ValueError("DXC returned non-SPIR-V or unsupported version")
    if bound == 0 or bound > 1_000_000 or schema != 0:
        raise ValueError("Invalid SPIR-V module header")


def _entries(bank: Path) -> tuple[dict, list[dict]]:
    manifest = json.loads((bank / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("platform") != "Xbox360" or manifest.get("bank_version") != 1:
        raise ValueError("Only original Hulk Xbox 360 shader banks supported")
    records = manifest.get("shader_containers")
    if not isinstance(records, list) or not records:
        raise ValueError("Bank has no verified Xenos shader containers")
    unique: dict[str, dict] = {}
    for row in records:
        digest, stage = row.get("sha256"), row.get("stage")
        if not isinstance(digest, str) or not SHA.fullmatch(digest):
            raise ValueError("Unsafe shader container digest")
        if stage not in ("VS", "PS"):
            raise ValueError("Unknown shader stage")
        relative = row.get("private_file")
        if relative != f"Xenos/{digest}.xenos.bin":
            raise ValueError("Invalid Xenos container path or identity")
        length = row.get("bytes")
        if not isinstance(length, int) or not 36 <= length <= MAX_INPUT:
            raise ValueError("Unsupported original shader length")
        src = (bank / relative).resolve()
        if bank.resolve() not in src.parents or not src.is_file():
            raise ValueError("Original Xenos shader file absent or unsafe link")
        data = src.read_bytes()
        if len(data) != length or hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("Private Xenos input SHA/length mismatch")
        prior = unique.get(digest)
        if prior is not None and prior["stage"] != stage:
            raise ValueError("Same Xenos source SHA used with conflicting stages")
        unique[digest] = row
    return manifest, [unique[key] for key in sorted(unique)]


def compile_bank(bank: Path, out: Path, xenos: Path, dxc: Path, common: Path,
                 repository: Path, limit: int = 0) -> dict:
    bank = _private(bank, repository)
    out = _private(out, repository)
    if not xenos.is_file() or not dxc.is_file() or not common.is_file():
        raise FileNotFoundError("XenosRecomp, DXC or shader_common.h not available")
    if limit < 0:
        raise ValueError("Negative shader count limit")
    manifest, entries = _entries(bank)
    if limit:
        entries = entries[:limit]
    out.mkdir(parents=True, exist_ok=True)
    success, errors = [], []
    for row in entries:
        sha, stage = row["sha256"], row["stage"]
        hlsl = out / f"{sha}.{stage.lower()}.hlsl"
        spv = out / f"{sha}.{stage.lower()}.spv"
        source = bank / row["private_file"]
        # XenosRecomp's single-container CLI writes HLSL, not SPIR-V.
        # DXC is the actual AOT compiler (same vs_6_0/ps_6_0 as upstream).
        try:
            translate = subprocess.run(
                [str(xenos), str(source), str(hlsl), str(common)],
                capture_output=True, timeout=120, check=False)
            if translate.returncode or not hlsl.is_file() or not hlsl.stat().st_size:
                raise RuntimeError(f"XenosRecomp exit={translate.returncode}")
            cmd = [str(dxc), str(hlsl), "-T", ("vs_6_0" if stage == "VS" else "ps_6_0"),
                   "-HV", "2021", "-all-resources-bound", "-spirv",
                   "-fvk-use-dx-layout", "-Qstrip_debug", "-Fo", str(spv)]
            if stage == "VS":
                cmd.append("-fvk-invert-y")
            result = subprocess.run(cmd, capture_output=True, timeout=120, check=False)
            if result.returncode or not spv.is_file():
                raise RuntimeError(f"DXC SPIR-V exit={result.returncode}")
            code = spv.read_bytes()
            _check_spirv(code)
            success.append({"source_sha256": sha, "stage": stage,
                            "source_effect": row["effect"],
                            "spirv_sha256": hashlib.sha256(code).hexdigest(),
                            "spirv_bytes": len(code),
                            "private_spirv": spv.name})
        except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
            spv.unlink(missing_ok=True)
            errors.append({"source_sha256": sha, "stage": stage,
                           "error": str(error)[:180]})
    report = {"version": 1, "source_bank_sha256": manifest["archive_sha256"],
              "input_count": len(entries), "compiled_count": len(success),
              "failed_count": len(errors), "compiled": success, "failed": errors,
              "driver_pipeline_created": False,
              "game_draw_shader_mapping_verified": False,
              "native_renderer_ready": False}
    # Manifest paths are private basename only: never export guest content.
    with tempfile.NamedTemporaryFile("w", dir=out, prefix=".hulk_aot_",
                                     suffix=".json", encoding="utf-8",
                                     delete=False) as tmp:
        temp = Path(tmp.name)
        json.dump(report, tmp, indent=2)
        tmp.write("\n")
    try:
        os.replace(temp, out / "aot_manifest.json")
    finally:
        temp.unlink(missing_ok=True)
    return report


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bank", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--xenosrecomp", type=Path, required=True)
    p.add_argument("--dxc", type=Path, required=True)
    p.add_argument("--common", type=Path, required=True)
    p.add_argument("--limit", type=int, default=0, help="0 = all; try 1 for first real shader")
    args = p.parse_args()
    report = compile_bank(args.bank, args.out, args.xenosrecomp, args.dxc,
                          args.common, Path(__file__).resolve().parents[1], args.limit)
    print(json.dumps({"attempted": report["input_count"],
                      "compiled": report["compiled_count"],
                      "failed": report["failed_count"],
                      "native_renderer_ready": False}))
    return 0 if not report["failed_count"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
