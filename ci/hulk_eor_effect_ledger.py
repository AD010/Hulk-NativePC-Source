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
import struct
import tempfile

MAGIC = b'HEORMAT2'

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
    shader_stages: dict[int, dict[str, list[tuple[int, bytes]]]] = defaultdict(
        lambda: {'VS': [], 'PS': []})
    seen = set()
    for row in entries:
        effect = row.get('effect')
        stage = row.get('stage')
        identity = row.get('sha256')
        relative_offset = row.get('relative_offset')
        if (not isinstance(effect, int) or effect <= 0 or effect > 0xffffffff or
                stage not in ('VS', 'PS') or not isinstance(identity, str) or
                len(identity) != 64 or any(c not in '0123456789abcdef' for c in identity) or
                identity in seen or not isinstance(relative_offset, int) or
                relative_offset < 0 or not isinstance(row.get('bytes'), int)):
            raise ValueError("Invalid original EoR stage/digest/offset")
        seen.add(identity)
        blob = (manifest.parent / 'Xenos' / (identity + '.xenos.bin')).read_bytes()
        if (len(blob) != row['bytes'] or len(blob) > 8 << 20 or
                hashlib.sha256(blob).hexdigest() != identity):
            raise ValueError("Private original Xenos shader container changed")
        shader_stages[effect][stage].append((relative_offset, bytes.fromhex(identity)))
    pairs = []
    for effect, stages in shader_stages.items():
        vertex = sorted(stages['VS'])
        pixel = sorted(stages['PS'])
        if len(vertex) != len(pixel) or not vertex:
            raise ValueError("EoR effect stage counts differ or stage absent")
        if len({p for p, _ in vertex}) != len(vertex) or len({p for p, _ in pixel}) != len(pixel):
            raise ValueError("Duplicate original EoR effect shader offset")
        if vertex[-1][0] >= pixel[0][0]:
            raise ValueError("EoR VS/PS archive grouping is not as verified")
        for ordinal, ((_, vs), (_, ps)) in enumerate(zip(vertex, pixel)):
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
