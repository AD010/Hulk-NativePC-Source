#!/usr/bin/env python3
"""Compile privately extracted ORIGINAL EoR Xenos shader containers to Vulkan SPIR-V.

This stage is independent of ReXGlue/Xenia/PM4 at RUNTIME. Build-time
XenosRecomp (adapted to Hulk) and DXC are external verified tools. Coverage
must be complete; this tool never fabricates shaders or publishes game bytes.
Compilation is not proof of correct EoR material/vertex layout bindings.
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

MAX_SHADERS = 16384
MAX_CONTAINER = 8 << 20
MAX_SPV = 8 << 20
MAGIC = b'HEORSPV1'
ADAPTER_MARKER = 'HULK_EOR_XENOSRECOMP_STRICT_LOD2D_V1'
HEX_SHA = re.compile(r'^[a-f0-9]{64}$')


def spirv_info(data: bytes, stage: str) -> int:
    if len(data) < 20 or len(data) % 4 or len(data) > MAX_SPV:
        raise ValueError('Truncated or oversized SPIR-V binary')
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    if (words[0] != 0x07230203 or not 0x10000 <= words[1] <= 0x10600
            or words[3] == 0 or words[3] > 1000000 or words[4] != 0):
        raise ValueError('Invalid SPIR-V header')
    expected_model = 0 if stage == 'VS' else 4
    model_count = 0
    found_entry = False
    cursor = 5
    while cursor < len(words):
        op = words[cursor] & 0xffff
        count = words[cursor] >> 16
        if count == 0 or count > len(words) - cursor:
            raise ValueError('Invalid SPIR-V instruction length')
        if op == 14:
            model_count += 1
            if count != 3:
                raise ValueError('Invalid SPIR-V memory model')
        if op == 15:
            if count < 4:
                raise ValueError('Invalid SPIR-V entry point')
            if words[cursor + 1] == expected_model:
                if words[cursor + 2] == 0 or words[cursor + 2] >= words[3]:
                    raise ValueError('Invalid SPIR-V entry point ID')
                name = struct.pack('<' + 'I' * (count - 3), *words[cursor + 3:cursor + count])
                if b'\0' not in name or name.split(b'\0', 1)[0] != b'main':
                    raise ValueError('Unexpected SPIR-V entry point')
                found_entry = True
        cursor += count
    if model_count != 1 or not found_entry:
        raise ValueError('Missing Vulkan shader entry or memory model')
    return len(words)


def checked(argv: list[str], *, timeout: int = 150) -> None:
    try:
        result = subprocess.run(argv, capture_output=True, text=True,
                                timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise RuntimeError(f'Shader compiler unavailable or timed out: {argv[0]}') from e
    if result.returncode:
        # External compiler may print private game paths; do not echo them.
        raise RuntimeError(f'{Path(argv[0]).name} failed, return code {result.returncode}')


def manifest_entries(manifest: dict, private_root: Path) -> list[tuple[str, str, Path]]:
    if manifest.get('platform') != 'Xbox360' or manifest.get('bank_version') != 1:
        raise ValueError('Unknown original ShaderEffects manifest')
    entries = manifest.get('shader_containers')
    if not isinstance(entries, list) or not 0 < len(entries) <= MAX_SHADERS:
        raise ValueError('No verified original Xenos shader containers')
    shaders = {}
    for record in entries:
        stage, digest = record.get('stage'), record.get('sha256')
        if stage not in ('VS', 'PS') or not isinstance(digest, str) or not HEX_SHA.fullmatch(digest):
            raise ValueError('Unverified source shader stage or digest')
        file = private_root / 'Xenos' / (digest + '.xenos.bin')
        if not file.is_file() or not 36 <= file.stat().st_size <= MAX_CONTAINER:
            raise ValueError('Original Xenos shader input missing or invalid')
        original = file.read_bytes()
        if hashlib.sha256(original).hexdigest() != digest or len(original) != record.get('bytes'):
            raise ValueError('Original Xenos container has changed')
        if digest in shaders and shaders[digest][0] != stage:
            raise ValueError('Original shader has conflicting stages')
        shaders[digest] = (stage, file)
    return [(digest, *shaders[digest]) for digest in sorted(shaders)]


def encode_bank(shaders: list[tuple[str, str, bytes]]) -> bytes:
    if not shaders or len(shaders) > MAX_SHADERS:
        raise ValueError('Invalid AOT bank coverage')
    output = bytearray(MAGIC + struct.pack('<II', len(shaders), len(shaders)))
    seen = set()
    for digest, stage, spirv in shaders:
        if digest in seen or stage not in ('VS', 'PS') or not HEX_SHA.fullmatch(digest):
            raise ValueError('Duplicate/invalid shader identity')
        seen.add(digest)
        count = spirv_info(spirv, stage)
        output.extend(bytes.fromhex(digest))
        output.extend(struct.pack('<II', 1 if stage == 'VS' else 2, count))
        output.extend(spirv)
    if len(output) > (1 << 30):
        raise ValueError('Private SPIR-V bank too large')
    return bytes(output)


def atomic_write(dest: Path, payload: bytes) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp_name = None
    try:
        with tempfile.NamedTemporaryFile(dir=dest.parent, prefix='.hulk_aot_',
                                         delete=False) as f:
            tmp_name = f.name
            f.write(payload)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp_name, dest)
    finally:
        if tmp_name:
            Path(tmp_name).unlink(missing_ok=True)


def compile_private_bank(manifest_path: Path, output_path: Path, *,
                         xenosrecomp: Path, dxc: Path, spirv_val: Path,
                         shader_common: Path, repo_root: Path) -> dict:
    out = output_path.resolve()
    repo = repo_root.resolve()
    if out == repo or repo in out.parents:
        raise ValueError('Proprietary shader bank must remain outside the public source repository')
    raw = json.loads(manifest_path.read_text(encoding='utf-8'))
    entries = manifest_entries(raw, manifest_path.parent)
    if not shader_common.is_file():
        raise ValueError('XenosRecomp shader_common.h absent')
    converted = []
    with tempfile.TemporaryDirectory(prefix='hulk_eor_shader_') as directory:
        temp = Path(directory)
        for index, (digest, stage, original) in enumerate(entries):
            hlsl = temp / f'{index}.hlsl'
            spv = temp / f'{index}.spv'
            checked([str(xenosrecomp), str(original), str(hlsl), str(shader_common)])
            if not hlsl.is_file():
                raise RuntimeError('XenosRecomp did not generate HLSL')
            generated = hlsl.read_text(encoding='utf-8')
            if 'void main(' not in generated or ADAPTER_MARKER not in generated:
                raise RuntimeError('Missing Hulk-patched XenosRecomp shader entry/adapter identity')
            args = [str(dxc), '-T', 'vs_6_0' if stage == 'VS' else 'ps_6_0',
                    '-E', 'main', '-HV', '2021', '-all-resources-bound',
                    '-spirv', '-fvk-use-dx-layout']
            if stage == 'VS':
                args.append('-fvk-invert-y')
            args += ['-Qstrip_debug', '-Fo', str(spv), str(hlsl)]
            checked(args)
            if not spv.is_file():
                raise RuntimeError('DXC produced no SPIR-V')
            code = spv.read_bytes()
            spirv_info(code, stage)
            checked([str(spirv_val), '--target-env', 'vulkan1.3', str(spv)])
            converted.append((digest, stage, code))
    output = encode_bank(converted)
    atomic_write(out, output)
    return {'source_shaders': len(entries), 'compiled': len(converted),
            'stages': {stage: sum(s == stage for _, s, _ in converted)
                       for stage in ('VS', 'PS')},
            'bank_sha256': hashlib.sha256(output).hexdigest(),
            'bank_format': 'HEORSPV1', 'runtime_material_binding_verified': False}


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    for arg in ('manifest', 'output', 'xenosrecomp', 'dxc', 'spirv-val', 'shader-common'):
        p.add_argument('--' + arg, type=Path, required=True)
    args = p.parse_args()
    result = compile_private_bank(args.manifest, args.output,
                                  xenosrecomp=args.xenosrecomp, dxc=args.dxc,
                                  spirv_val=args.spirv_val,
                                  shader_common=args.shader_common,
                                  repo_root=Path(__file__).resolve().parents[1])
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
