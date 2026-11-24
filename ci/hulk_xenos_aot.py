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
from hulk_eor_vulkan_hlsl import native_resources
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


def _source_shaders_for_effects(source: Path, effects: set[int]) -> set[str]:
    """Exact stage SHA pairs from private authentic HEORMAT2, never names."""
    data = source.read_bytes()
    if len(data) < 16 or data[:8] != b"HEORMAT2":
        raise ValueError("Verified original HEORMAT2 effect ledger missing")
    count, version = struct.unpack_from("<II", data, 8)
    if version != 2 or not 0 < count <= 16384 or len(data) != 16 + 72 * count:
        raise ValueError("Malformed original effect/pass shader digest bank")
    found: set[int] = set()
    selected: set[str] = set()
    for i in range(count):
        at = 16 + 72 * i
        effect, ordinal = struct.unpack_from("<II", data, at)
        if effect not in effects:
            continue
        if ordinal > 16384:
            raise ValueError("Unsafe original effect pass")
        found.add(effect)
        selected.add(data[at + 8:at + 40].hex())
        selected.add(data[at + 40:at + 72].hex())
    if found != effects:
        raise ValueError("Requested source effect hash is not in original HEORMAT2")
    return selected


def _source_shaders_for_passes(
    source: Path, original_passes: set[tuple[int, int]]) -> set[str]:
    """Choose ONLY the exact VS/PS pair of real, live, observed EoR Draws."""
    data = source.read_bytes()
    if len(data) < 16 or data[:8] != b"HEORMAT2":
        raise ValueError("Verified original HEORMAT2 ledger absent")
    count, version = struct.unpack_from("<II", data, 8)
    if version != 2 or not 0 < count <= 16384 or len(data) != 16 + 72 * count:
        raise ValueError("Malformed original source pass pair ledger")
    found: set[tuple[int,int]] = set()
    programs: set[str] = set()
    for ordinal in range(count):
        at = 16 + 72 * ordinal
        effect, pass_index = struct.unpack_from("<II", data, at)
        key = (effect, pass_index)
        if key not in original_passes:
            continue
        found.add(key)
        programs.add(data[at + 8:at + 40].hex())
        programs.add(data[at + 40:at + 72].hex())
    if found != original_passes:
        raise ValueError("Observed XDK effect/pass is absent from original ShaderEffects")
    return programs


def compile_bank(bank: Path, out: Path, xenos: Path, dxc: Path, common: Path,
                 repository: Path, limit: int = 0, workers: int = 0,
                 effect_catalog: Path | None = None,
                 effect_hashes: tuple[int, ...] = (),
                 resume: bool = True,
                 effect_passes: tuple[tuple[int, int], ...] = ()) -> dict:
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
    # Keep the complete authenticated source inventory even when --limit or
    # --effect-hash selects a smaller incremental compilation batch.
    full_index = {(row["sha256"], row["stage"]): row for row in entries}
    full_count = len(full_index)
    if effect_hashes or effect_passes:
        if effect_catalog is None:
            raise ValueError("Original HEORMAT2 path required for effect-targeted AOT")
        wanted = set(effect_hashes)
        wanted_passes = set(effect_passes)
        if any(not 0 < v < 2**32 for v in wanted) or any(
                not 0 < effect < 2**32 or not 0 <= ordinal < 16384
                for effect, ordinal in wanted_passes):
            raise ValueError("Invalid source effect/pass identity")
        effect_sources = (
            _source_shaders_for_effects(effect_catalog, wanted) if wanted else set())
        if wanted_passes:
            effect_sources.update(
                _source_shaders_for_passes(effect_catalog, wanted_passes))
        entries = [row for row in entries if row["sha256"] in effect_sources]
        if {row["sha256"] for row in entries} != effect_sources:
            raise ValueError(
                "Original XDK shader pair references source missing from private bank")
    if limit:
        entries = entries[:limit]
    out.mkdir(parents=True, exist_ok=True)
    success, errors = [], []
    # All three build-time tools contribute to the exact HLSL/SPIR-V
    # semantics. Never reuse a private .spv from another translator,
    # DXC revision or shared shader constants. The original Xenos bytes
    # themselves are already SHA-256 validated by _entries().
    toolchain_sha256 = {
        "xenosrecomp": hashlib.sha256(xenos.read_bytes()).hexdigest(),
        "dxc": hashlib.sha256(dxc.read_bytes()).hexdigest(),
        "shader_common": hashlib.sha256(common.read_bytes()).hexdigest(),
        "native_hlsl": hashlib.sha256(
            Path(__file__).with_name("hulk_eor_vulkan_hlsl.py").read_bytes()).hexdigest(),
        "aot_driver": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    prior_compiled: dict[tuple[str, str], dict] = {}
    prior_manifest = out / "aot_manifest.json"
    if resume and prior_manifest.is_file():
        try:
            previous=json.loads(prior_manifest.read_text(encoding="utf-8"))
            if (previous.get("source_bank_sha256")==manifest["archive_sha256"]
                    and previous.get("toolchain_sha256")==toolchain_sha256):
                for item in previous.get("compiled", []):
                    if not isinstance(item, dict):
                        continue
                    sha, stage = item.get("source_sha256"), item.get("stage")
                    if (not isinstance(sha, str) or not SHA.fullmatch(sha)
                            or stage not in ("VS", "PS")
                            or (sha, stage) not in full_index
                            or item.get("private_spirv") != f"{sha}.{stage.lower()}.spv"):
                        continue
                    # A previous manifest is never enough to authenticate
                    # an executable program: re-read and inspect its bytes
                    # before allowing carry-over to a new partial AOT run.
                    try:
                        data = (out / item["private_spirv"]).read_bytes()
                        if (len(data) != item.get("spirv_bytes") or
                            hashlib.sha256(data).hexdigest() !=
                                item.get("spirv_sha256")):
                            continue
                        _check_spirv(data)
                        spirv_info(data, stage)
                        validate_named_spirv_images(data)
                        prior_compiled[(sha, stage)] = item
                    except (OSError, ValueError, TypeError):
                        continue
        except (OSError, ValueError, TypeError):
            prior_compiled={}  # stale/corrupt output never authorizes AOT

    def compile_one(row: dict) -> tuple[dict | None, dict | None]:
        sha, stage = row["sha256"], row["stage"]
        hlsl = out / f"{sha}.{stage.lower()}.hlsl"
        spv = out / f"{sha}.{stage.lower()}.spv"
        source = bank / row["private_file"]
        # Reuse an EXACT previously compiled original Xenos program only
        # when the source archive and EVERY compiler tool hash matches.
        # The output is independently revalidated by stage, SPIR-V opcode
        # structure, reflection contract, length and recorded SHA-256.
        previous=prior_compiled.get((sha,stage))
        if previous and previous.get("private_spirv")==spv.name:
            try:
                compiled=spv.read_bytes()
                if (len(compiled)==previous.get("spirv_bytes") and
                    hashlib.sha256(compiled).hexdigest()==
                        previous.get("spirv_sha256")):
                    _check_spirv(compiled)
                    spirv_info(compiled,stage)
                    validate_named_spirv_images(compiled)
                    return ({**previous,"reused_verified": True},None)
            except (OSError, ValueError, TypeError):
                pass  # corrupt/obsolete shader is recompiled, never loaded
        # Each digest has isolated files; never accept a stale output after
        # an unsuccessful compiler attempt.
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
            hlsl.write_text(native_resources(generated, stage), encoding="utf-8")
            cmd = [str(dxc), str(hlsl), "-T", ("vs_6_0" if stage == "VS" else "ps_6_0"),
                   "-E", "main", "-HV", "2021", "-all-resources-bound", "-spirv",
                   "-fvk-use-dx-layout", "-fspv-reflect", "-Fo", str(spv)]
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

    def checkpoint_verified_progress() -> None:
        """Durably checkpoint authenticated AOT work before a long job ends.

        The complete 5,194-source conversion can outlast a CI/Work session.
        Previously a killed process left .spv files but no trusted manifest,
        forcing all successful shader programs to be rebuilt next session.
        A checkpoint is NOT a HEORSPV1 bank and never enables rendering.
        """
        staged = dict(prior_compiled)
        for bad in errors:
            staged.pop((bad["source_sha256"], bad["stage"]), None)
        for good in success:
            staged[(good["source_sha256"], good["stage"])] = good
        rows = sorted(staged.values(),
                      key=lambda item: item["source_sha256"])
        saved = {
            "version": 1,
            "source_bank_sha256": manifest["archive_sha256"],
            "toolchain_sha256": toolchain_sha256,
            "input_count": len(entries),
            "workers_used": active_workers,
            "compiled_count": len(rows),
            "selected_compiled_count": len(success),
            "failed_count": len(errors),
            "compiled": rows, "failed": errors,
            "in_progress": True,
            "native_renderer_ready": False,
        }
        with tempfile.NamedTemporaryFile(
                "w", dir=out, prefix=".hulk_aot_checkpoint_",
                suffix=".json", encoding="utf-8", delete=False) as tmp:
            checkpoint_path = Path(tmp.name)
            json.dump(saved, tmp, indent=2)
            tmp.write("\n")
            tmp.flush()
            os.fsync(tmp.fileno())
        try:
            os.replace(checkpoint_path, prior_manifest)
        finally:
            checkpoint_path.unlink(missing_ok=True)

    # executor.map returns results in input order, even when subprocesses
    # finish out of order. Manifest and binary bank are reproducible.
    with ThreadPoolExecutor(max_workers=active_workers,
                            thread_name_prefix="hulk-xenos-aot") as executor:
        for completed, (good, error) in enumerate(
                executor.map(compile_one, entries), start=1):
            if good is not None:
                success.append(good)
            if error is not None:
                errors.append(error)
            # Keep work across a killed Windows runner or a Work session
            # limit without O(5194^2) writes. Every recovered item is still
            # independently SHA/stage/SPIR-V validated on the next run.
            if completed % 32 == 0:
                checkpoint_verified_progress()
    # Merge successful selected shaders with the still-verified results of
    # earlier batches. Previous --limit / --effect-hash invocations used to
    # OVERWRITE the cumulative bank with only that subset, discarding hours
    # of verified original SPIR-V work and defeating --resume.
    selected_keys = {(row["sha256"], row["stage"]) for row in entries}
    cumulative = {key: item for key, item in prior_compiled.items()
                  if key not in selected_keys}
    cumulative.update({(item["source_sha256"], item["stage"]): item
                       for item in success})
    all_success = sorted(cumulative.values(),
                         key=lambda item: item["source_sha256"])
    # NativePC binary SPIR-V bank: no JSON parser or runtime DXC required.
    # The expected count is ALWAYS the complete authenticated original bank.
    # Partial compilation is represented honestly as incomplete coverage.
    pack_target = out / "hulk_shader_bank.bin"
    with tempfile.NamedTemporaryFile("wb", dir=out, prefix=".hulk_spv_",
                                     delete=False) as tmp:
        pack_temp = Path(tmp.name)
        tmp.write(b"HEORSPV1")
        tmp.write(struct.pack("<II", full_count, len(all_success)))
        for item in all_success:
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
              "compiled_count": len(all_success),
              "selected_compiled_count": len(success),
              "reused_verified_count": sum(bool(x.get("reused_verified"))
                                           for x in success),
              "toolchain_sha256": toolchain_sha256,
              "failed_count": len(errors), "compiled": all_success, "failed": errors,
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
    p.add_argument("--limit", type=int, default=0,
                   help="0 = all selected shaders; use a small limit for first compilation")
    p.add_argument("--effect-catalog", type=Path,
                   help="Private source-verified HEORMAT2.bin effect/pass ledger")
    p.add_argument("--effect-hash", action="append", type=lambda s:int(s,0),
                   default=[], help="Compile all source passes of an original ShaderEffects hash")
    p.add_argument("--effect-pass", action="append", default=[],
                   help="Compile ONLY this source-observed effect:pass, e.g. 0x23B32:2")
    p.add_argument("--observed-passes", type=Path,
                   help="Private LIVE XDK worker demand file, effect:pass per line")
    p.add_argument("--resume", action="store_true", default=True,
                   help="reuse only SHA-verified results from this exact source/toolchain (default)")
    p.add_argument("--no-resume", action="store_false", dest="resume",
                   help="force rebuilding every original shader from verified Xenos input")
    p.add_argument("--workers", type=int, default=0,
                   help="AOT subprocess concurrency: auto (0) reserves two logical CPUs, max 8; explicit 1..16")
    args = p.parse_args()
    selected = list(args.effect_pass)
    if args.observed_passes:
        selected.extend(
            row.strip() for row in args.observed_passes.read_text(
                encoding="ascii").splitlines() if row.strip())
    def source_pass(value: str) -> tuple[int,int]:
        parts = value.split(":")
        if len(parts) != 2:
            raise ValueError("Source XDK shader demand must be effect:pass")
        return (int(parts[0],0),int(parts[1],0))
    report = compile_bank(args.bank, args.out, args.xenosrecomp, args.dxc,
                          args.common, Path(__file__).resolve().parents[1], args.limit,
                          args.workers, args.effect_catalog,
                          tuple(args.effect_hash), args.resume,
                          tuple(source_pass(item) for item in selected))
    print(json.dumps({"attempted": report["input_count"],
                      "workers": report["workers_used"],
                      "compiled": report["compiled_count"],
                      "failed": report["failed_count"],
                      "native_renderer_ready": False}))
    return 0 if not report["failed_count"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
