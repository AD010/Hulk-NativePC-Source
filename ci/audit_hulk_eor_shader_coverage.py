#!/usr/bin/env python3
"""Source-safe static EoR ShaderEffects opcode audit (no original shader bytes).

This is NOT a runtime translator. Counts only direct CF EXEC fetch sites;
conditional and dynamic-flow work remains unsupported for full shader parity.
Requires the private manifest produced by ci/hulk_shader_effect_bank.py.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

FETCH_TYPES = {0: 'vertex_fetch', 1: 'texture_fetch', 16: 'border_color',
               17: 'computed_lod', 18: 'gradients', 19: 'weights',
               24: 'set_lod', 25: 'set_gradient_x', 26: 'set_gradient_y'}
EXEC_TYPES = frozenset((1, 2, 3, 4, 5, 6, 13, 14))
DYNAMIC_TYPES = frozenset((7, 8, 9, 10, 11))


def inspect_shader(blob: bytes, stage: str) -> dict:
    if stage not in ('VS', 'PS') or len(blob) < 60:
        raise ValueError('Invalid shader stage/length')
    flags, virtual, physical, _fc, ct, dt, so, f1, f2 = struct.unpack_from('>9I', blob)
    if (flags & 0xffffff00) != 0x102a1100 or ('VS' if flags & 1 else 'PS') != stage:
        raise ValueError('Invalid original Xenos shader identity')
    if (virtual < 36 or physical == 0 or virtual + physical != len(blob)
            or not (36 <= ct < virtual and 36 <= so < virtual)):
        raise ValueError('Invalid original shader data extent')
    if so + 24 > virtual or f1 or f2 or (dt and not 36 <= dt < virtual):
        raise ValueError('Invalid original shader reflection')
    physical_offset, size = struct.unpack_from('>II', blob, so)
    if size == 0 or size % 12 or physical_offset + size > physical:
        raise ValueError('Invalid original Xenos instruction extent')
    code = struct.unpack_from('>' + 'I' * (size // 4), blob, virtual + physical_offset)
    instructions = len(code) // 3
    limit = instructions
    flow = []
    for index in range(instructions):
        a, b, c = code[3 * index:3 * index + 3]
        for word0, word1 in ((a, b & 0xffff),
                             ((b >> 16) | ((c & 0xffff) << 16), c >> 16)):
            op = (word1 >> 12) & 15
            flow.append((index, op, word0))
            if op in EXEC_TYPES:
                limit = min(limit, word0 & 0xfff)
        if index + 1 >= limit:
            break
    fetches, cf = Counter(), Counter()
    visited = set()
    for index, op, word0 in flow:
        if index >= limit:
            continue
        cf[op] += 1
        if op not in EXEC_TYPES:
            continue
        address, count, sequence = word0 & 0xfff, (word0 >> 12) & 7, (word0 >> 16) & 0xfff
        if address > instructions or count > instructions - address:
            raise ValueError('Xenos EXEC references out-of-range bytecode')
        for j in range(count):
            if not (sequence & (1 << (2 * j))):
                continue
            at = address + j
            if at in visited:
                continue
            visited.add(at)
            fetches[code[3 * at] & 31] += 1
    return {'stage': stage, 'direct_fetches': dict(fetches),
            'control_flow_types': dict(cf),
            'dynamic_control_flow_present': any(op in DYNAMIC_TYPES for op in cf)}


def audit(manifest_path: Path) -> dict:
    meta = json.loads(manifest_path.read_text(encoding='utf-8'))
    if meta.get('bank_version') != 1 or meta.get('platform') != 'Xbox360':
        raise ValueError('Unverified private EoR manifest')
    records = meta.get('shader_containers')
    if not isinstance(records, list) or not 0 < len(records) <= 16384:
        raise ValueError('Invalid original shader count')
    stages, fetches, flows, effects, seen = Counter(), Counter(), Counter(), set(), set()
    dynamic = 0
    static_fetch_candidates = 0
    static_unimplemented = 0
    effects_with_unimplemented = set()
    # Direct EXEC fetch opcode screen only, not compilation or shader parity.
    supported_direct_fetches = frozenset((0, 1, 19))
    for record in records:
        stage, digest = record['stage'], record['sha256']
        if (not isinstance(digest, str) or len(digest) != 64 or
            any(c not in '0123456789abcdef' for c in digest) or digest in seen):
            raise ValueError('Duplicate or invalid original shader SHA-256')
        seen.add(digest)
        path = manifest_path.parent / 'Xenos' / (digest + '.xenos.bin')
        original = path.read_bytes()
        if (len(original) > (8 << 20) or len(original) != record['bytes'] or
                hashlib.sha256(original).hexdigest() != digest):
            raise ValueError('Original private shader changed')
        result = inspect_shader(original, stage)
        stages[stage] += 1
        effects.add(record['effect'])
        fetches.update(result['direct_fetches'])
        flows.update(result['control_flow_types'])
        dynamic += result['dynamic_control_flow_present']
        unsupported = set(result['direct_fetches']) - supported_direct_fetches
        if unsupported:
            static_unimplemented += 1
            effects_with_unimplemented.add(record['effect'])
        if not unsupported and not result['dynamic_control_flow_present']:
            static_fetch_candidates += 1
    return {'original_shader_containers': len(records),
            'effects_with_shaders': len(effects),
            'stages': dict(stages),
            'direct_exec_fetch_opcode_counts': dict(fetches),
            'opcode_names': FETCH_TYPES,
            'control_flow_opcode_counts': dict(flows),
            'shaders_with_dynamic_control_flow': dynamic,
            'static_direct_exec_fetch_candidate_shaders': static_fetch_candidates,
            'shaders_with_unimplemented_direct_fetch': static_unimplemented,
            'effects_with_unimplemented_direct_fetch': len(effects_with_unimplemented),
            'candidate_is_compiled': False,
            'scope': 'direct static EXEC paths only; no native Draw ABI or pixel correctness proof'}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--private-manifest', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.private_manifest), sort_keys=True, indent=2))


if __name__ == '__main__':
    main()
