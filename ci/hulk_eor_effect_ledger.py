#!/usr/bin/env python3
"""Private EoR ShaderEffects VS/PS *ordinal candidates* to HEORMAT2.

Original archive groups all VS then all PS for each effect; verified counts
match. Pairing matching ordinal positions is a candidate, not proof of which
technique/pass draws them or the original EoR rendering state. Require an
original per-draw pass identity before using the ledger in a Vulkan backend.
Never publish proprietary Xenos shader bytes or metadata to the source repo.
"""
from __future__ import annotations
import argparse
from collections import defaultdict
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile
import zlib

from hulk_shader_effect_bank import scan_xenos_containers

MAGIC = b'HEORMAT2'
PDB_NAME = re.compile(rb'([A-Za-z0-9_]+_[vf]p\.updb)')


def _effect_id(value: object) -> int:
    # parse_index emits eight hexadecimal digits, including leading zeros.
    # Integer IDs also occur in earlier manifests.
    if isinstance(value, str) and re.fullmatch(r'[0-9A-Fa-f]{8}', value):
        value = int(value, 16)
    if type(value) is not int or not 0 < value <= 0xffffffff:
        raise ValueError('Invalid original effect source identity')
    return value


def _effect_sources(manifest: Path, records: list[dict]) -> tuple[
        dict[int, str], dict[tuple[int, int], tuple[str, str]]]:
    """Verify actual original effect names by source file digest and EoR CRC32.

    This metadata is never serialized into the public repository. A source
    archive absent from a synthetic ledger cannot assert PDB technique proof.
    """
    found: dict[int, str] = {}
    containers = {}
    for row in records:
        raw_key = row.get('hash')
        key = _effect_id(raw_key)
        digest, size = row.get('sha256'), row.get('bytes')
        if (not isinstance(digest, str) or len(digest) != 64 or
                any(c not in '0123456789abcdef' for c in digest) or
                not isinstance(size, int) or size < 20 or size > (2 << 20) or
                row.get('private_file') != f'{raw_key}.effect.bin' or
                key in found):
            raise ValueError('Invalid original effect source identity')
        payload = (manifest.parent / row['private_file']).read_bytes()
        if len(payload) != size or hashlib.sha256(payload).hexdigest() != digest:
            raise ValueError('Original ShaderEffect source record digest mismatch')
        end = payload.find(b'\0', 0, 129)
        if end < 1:
            raise ValueError('Original ShaderEffect name is unbounded')
        try:
            name = payload[:end].decode('ascii')
        except UnicodeDecodeError as exc:
            raise ValueError('Non-ASCII source effect name') from exc
        if (not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', name) or
                (zlib.crc32(name.upper().encode('ascii')) & 0xffffffff) != key):
            raise ValueError('Original EoR ShaderEffect name/hash mismatch')
        found[key] = name
        for offset, stage, shader in scan_xenos_containers(payload):
            containers[key, offset] = (stage, hashlib.sha256(shader).hexdigest())
    return found, containers


def _shader_technique(blob: bytes, stage: str, effect_name: str) -> str:
    """Resolve exactly one original XDK debug program's technique by name.

    An ordered VS/PS pair must belong to the SAME authored technique.
    Merely zipping all VS then all PS is insufficient material evidence.
    """
    if len(blob) < 68:
        raise ValueError('Original shader container too short for debug identity')
    fields = struct.unpack_from('>9I', blob)
    flags, virtual, physical = fields[:3]
    if ((flags & 0xffffff00) != 0x102a1100 or
            ('VS' if flags & 1 else 'PS') != stage or
            virtual < 36 or physical == 0 or virtual + physical != len(blob)):
        raise ValueError('Invalid original Xenos stage/length for technique')
    candidates = [m.group(1).decode('ascii')
                  for m in PDB_NAME.finditer(blob[36:virtual])]
    if len(candidates) != 1:
        raise ValueError('Missing or ambiguous original shader debug technique')
    name = candidates[0]
    suffix = '_vp.updb' if stage == 'VS' else '_fp.updb'
    prefix = effect_name + '_'
    if not name.startswith(prefix) or not name.endswith(suffix):
        raise ValueError('Original shader program belongs to a different EoR effect/stage')
    tail = name[len(prefix):-len(suffix)]
    technique, sep, shader = tail.partition('_')
    if not sep or not technique or not shader:
        raise ValueError('Missing original authored shader technique/program')
    return technique


def generate(manifest: Path, destination: Path, source_root: Path) -> dict:
    manifest = manifest.resolve()
    destination = destination.resolve()
    source_root = source_root.resolve()
    if destination == source_root or source_root in destination.parents:
        raise ValueError("Private EoR material ledger cannot be stored in public source")
    info = json.loads(manifest.read_text(encoding='utf-8'))
    entries = info.get('shader_containers')
    if (info.get('bank_version') != 1 or info.get('platform') != 'Xbox360' or
            not isinstance(entries, list) or not 0 < len(entries) <= 16384):
        raise ValueError("Unverified original EoR effect manifest")
    # Real extracted manifests always include independently source-hashed
    # effect records. Synthetic callers without records retain candidate
    # pairing, but cannot claim that techniques were independently proved.
    records = info.get('records')
    if records is not None and not isinstance(records, list):
        raise ValueError('Malformed original ShaderEffects source record ledger')
    effect_names, embedded = _effect_sources(manifest, records) if records is not None else ({}, {})
    shader_stages: dict[int, dict[str, list[tuple[int, bytes, str | None]]]] = defaultdict(
        lambda: {'VS': [], 'PS': []})
    # Original shader containers may be repeated at several effect/pass
    # locations. Reuse a SINGLE AOT module for the same source SHA/stage.
    seen: dict[str, str] = {}
    for row in entries:
        effect = _effect_id(row.get('effect'))
        stage = row.get('stage')
        identity = row.get('sha256')
        relative_offset = row.get('relative_offset')
        if (stage not in ('VS', 'PS') or not isinstance(identity, str) or
                len(identity) != 64 or any(c not in '0123456789abcdef' for c in identity) or
                (identity in seen and seen[identity] != stage) or
                not isinstance(relative_offset, int) or
                relative_offset < 0 or not isinstance(row.get('bytes'), int)):
            raise ValueError("Invalid original EoR stage/digest/offset")
        seen[identity] = stage
        blob = (manifest.parent / 'Xenos' / (identity + '.xenos.bin')).read_bytes()
        if (len(blob) != row['bytes'] or len(blob) > 8 << 20 or
                hashlib.sha256(blob).hexdigest() != identity):
            raise ValueError("Private original Xenos shader container changed")
        if records is not None and effect not in effect_names:
            raise ValueError('Original ShaderEffect record for shader is missing')
        if records is not None and embedded.pop((effect, relative_offset), None) != (stage, identity):
            raise ValueError('Shader does not match the recorded location in its original effect')
        technique = (_shader_technique(blob, stage, effect_names[effect])
                     if records is not None else None)
        shader_stages[effect][stage].append(
            (relative_offset, bytes.fromhex(identity), technique))
    if embedded:
        raise ValueError('Manifest omits shaders from the original effect records')
    pairs = []
    proven_techniques = 0
    for effect, stages in shader_stages.items():
        vertex = sorted(stages['VS'])
        pixel = sorted(stages['PS'])
        if len(vertex) != len(pixel) or not vertex:
            raise ValueError("EoR effect stage counts differ or stage absent")
        if len({offset for offset, _, _ in vertex}) != len(vertex) or len({offset for offset, _, _ in pixel}) != len(pixel):
            raise ValueError("Duplicate original EoR effect shader offset")
        if vertex[-1][0] >= pixel[0][0]:
            raise ValueError("EoR VS/PS archive grouping is not as verified")
        for ordinal, ((_, vs, vs_name), (_, ps, ps_name)) in enumerate(
                zip(vertex, pixel)):
            if records is not None:
                if not vs_name or vs_name != ps_name:
                    raise ValueError('Original EoR VS/PS source technique names do not match')
                proven_techniques += 1
            pairs.append((effect, ordinal, vs, ps))
    pairs.sort()
    if len(pairs) * 2 != len(entries):
        raise ValueError("Original EoR shader coverage incomplete")
    output = bytearray(MAGIC + struct.pack('<II', len(pairs), 2))
    for effect, ordinal, vs, ps in pairs:
        output.extend(struct.pack('<II', effect, ordinal))
        output.extend(vs)
        output.extend(ps)
    destination.parent.mkdir(parents=True, exist_ok=True)
    name = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix='.eor_pair_',
                                         delete=False) as f:
            name = f.name
            f.write(output)
            f.flush()
            os.fsync(f.fileno())
        os.replace(name, destination)
    finally:
        if name:
            Path(name).unlink(missing_ok=True)
    return {'format': 'HEORMAT2', 'effects': len(shader_stages),
            'ordinal_pair_candidates': len(pairs),
            'original_shader_containers': len(entries),
            'all_runtime_passes_verified': False,
            'source_pdb_technique_pairs_verified': proven_techniques,
            'sha256': hashlib.sha256(output).hexdigest()}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--private-output', type=Path, required=True)
    a = p.parse_args()
    print(json.dumps(generate(a.manifest, a.private_output,
                              Path(__file__).resolve().parents[1]), sort_keys=True))

if __name__ == '__main__':
    main()
