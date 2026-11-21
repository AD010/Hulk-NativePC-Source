#!/usr/bin/env python3
"""Private Hulk XenosRecomp -> HLSL -> DXC/SPIR-V toolchain.

Uses actual SHA-256-verified containers extracted by hulk_shader_effect_bank.py.
No original game binaries, HLSL, SPIR-V or shader cache are written into Git.
XenosRecomp is game-specific: conversion success alone is not render parity.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from hulk_spirv_resource_contract import validate_named_spirv_images
from hulk_xenos_to_spirv import (
    spirv_info, ADAPTER_MARKER, GRADIENT_ADAPTER_MARKER)

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
                 repository: Path, limit: int = 0, workers: int = 0) -> dict:
    bank = _private(bank, repository)
    out = _private(out, repository)
    if not xenos.is_file() or not dxc.is_file() or not common.is_file():
        raise FileNotFoundError("XenosRecomp, DXC or shader_common.h not available")
    if limit < 0:
        raise ValueError("Negative shader count limit")
    if not isinstance(workers, int) or not 0 <= workers <= 16:
        raise ValueError("Shader compilation workers must be 0..16")
    # AOT jobs are independent external XenosRecomp/DXC processes. Reserve
    # host cores for Windows, I/O and other tasks, and limit memory pressure.
    # This does NOT modify guest/Havok physics threads or game FPS.
    logical = os.cpu_count() or 1
    active_workers = max(1, min(workers or max(1, logical - 2), 8))
    manifest, entries = _entries(bank)
    if limit:
        entries = entries[:limit]
    out.mkdir(parents=True, exist_ok=True)
    success, errors = [], []

    def compile_one(row: dict) -> tuple[dict | None, dict | None]:
        sha, stage = row["sha256"], row["stage"]
        hlsl = out / f"{sha}.{stage.lower()}.hlsl"
        spv = out / f"{sha}.{stage.lower()}.spv"
        source = bank / row["private_file"]
        # The input bank is validated before workers start. Each digest owns
        # its own two output files; no shared temporary source or DXC state.
        # On retry, never accept stale output left by a previous invocation.
        hlsl.unlink(missing_ok=True)
        spv.unlink(missing_ok=True)
        try:
            translate = subprocess.run(
                [str(xenos), str(source), str(hlsl), str(common)],
                capture_output=True, timeout=120, check=False)
            if translate.returncode or not hlsl.is_file() or not hlsl.stat().st_size:
                raise RuntimeError(f"XenosRecomp exit={translate.returncode}")
            # The parallel compiler must obey the EXACT same original
            # Hulk Xenos LOD/grad translation contract as the strict
            # HEORSPV1 complete-bank compiler. A stale/unpatched translator
            # can emit valid-looking but wrong textures, then be cached by SHA.
            generated=hlsl.read_text(encoding="utf-8")
            if ("void main(" not in generated or
                ADAPTER_MARKER not in generated or
                GRADIENT_ADAPTER_MARKER not in generated):
                raise RuntimeError(
                    "Missing Hulk original XenosRecomp LOD/gradient shader adapter")
            # DXC requires an explicit shader entry point. The original
            # complete-bank compiler already supplies -E main; without it
            # this incremental AOT route may reject valid XenosRecomp HLSL.
            cmd = [str(dxc), str(hlsl), "-T", ("vs_6_0" if stage == "VS" else "ps_6_0"),
                   "-E", "main", "-HV", "2021", "-all-resources-bound", "-spirv",
                   "-fvk-use-dx-layout", "-fspv-reflect", "-Fo", str(spv)]
            if stage == "VS":
                cmd.append("-fvk-invert-y")
            result = subprocess.run(cmd, capture_output=True, timeout=120, check=False)
            if result.returncode or not spv.is_file():
                raise RuntimeError(f"DXC SPIR-V exit={result.returncode}")
            code = spv.read_bytes()
            _check_spirv(code)
            # The incremental compiler must obey the SAME stage and
            # instruction-extent contract as the complete private AOT bank.
            spirv_info(code, stage)
            validate_named_spirv_images(code)
            return ({"source_sha256": sha, "stage": stage,
                     "source_effect": row["effect"],
                     "spirv_sha256": hashlib.sha256(code).hexdigest(),
                     "spirv_bytes": len(code), "private_spirv": spv.name}, None)
        except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
            spv.unlink(missing_ok=True)
            return (None, {"source_sha256": sha, "stage": stage,
                           "error": str(error)[:180]})

    # executor.map returns results in input order, even when subprocesses
    # finish out of order. Manifest and binary bank are reproducible.
    with ThreadPoolExecutor(max_workers=active_workers,
                            thread_name_prefix="hulk-xenos-aot") as executor:
        for good, error in executor.map(compile_one, entries):
            if good is not None:
                success.append(good)
            if error is not None:
                errors.append(error)
    # NativePC binary SPIR-V bank: no JSON parser or runtime DXC required.
    # The expected count is the FULL original source bank, not the --limit
    # smoke-test count; partial precompilation cannot become "ready".
    full_count = len(_entries(bank)[1])
    pack_target = out / "hulk_shader_bank.bin"
    with tempfile.NamedTemporaryFile("wb", dir=out, prefix=".hulk_spv_",
                                     delete=False) as tmp:
        pack_temp = Path(tmp.name)
        tmp.write(b"HEORSPV1")
        tmp.write(struct.pack("<II", full_count, len(success)))
        for item in sorted(success, key=lambda entry: entry["source_sha256"]):
            source_sha = bytes.fromhex(item["source_sha256"])
            shader = (out / item["private_spirv"]).read_bytes()
            _check_spirv(shader)
            tmp.write(source_sha)
            tmp.write(struct.pack("<II", 1 if item["stage"] == "VS" else 2,
                                  len(shader) // 4))
            tmp.write(shader)
    try:
        os.replace(pack_temp, pack_target)
    finally:
        pack_temp.unlink(missing_ok=True)
    report = {"version": 1, "source_bank_sha256": manifest["archive_sha256"],
              "input_count": len(entries), "workers_used": active_workers,
              "compiled_count": len(success),
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
    p.add_argument("--workers", type=int, default=0,
                   help="AOT subprocess concurrency: auto (0) reserves two logical CPUs, max 8; explicit 1..16")
    args = p.parse_args()
    report = compile_bank(args.bank, args.out, args.xenosrecomp, args.dxc,
                          args.common, Path(__file__).resolve().parents[1], args.limit,
                          args.workers)
    print(json.dumps({"attempted": report["input_count"],
                      "workers": report["workers_used"],
                      "compiled": report["compiled_count"],
                      "failed": report["failed_count"],
                      "native_renderer_ready": False}))
    return 0 if not report["failed_count"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
