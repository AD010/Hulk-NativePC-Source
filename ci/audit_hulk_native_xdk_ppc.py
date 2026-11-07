#!/usr/bin/env python3
"""Read-only XDK 6534 PPC evidence from ReXGlue's generated C++.

No guest functions are renamed, hooked, patched or assumed to be D3D calls.
The generated source must come from the supplied --xex (CI pins its SHA).
Output is developer analysis only, never a runtime profile or shipped asset.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
from pathlib import Path
import re

FUNC = re.compile(r'^DEFINE_REX_FUNC\(sub_([0-9A-Fa-f]{8})\)')
LABEL = re.compile(r'^\s*loc_([0-9A-Fa-f]{8}):')
ASM = re.compile(r'^\s*// (.+?)\s*$')
BRANCH = re.compile(r'^(bl|b)\s+(?:0x)?([0-9A-Fa-f]{8})\s*$')
LIS = re.compile(r'^lis r(\d+),\s*(-?\d+)\s*$')
ORI = re.compile(r'^(ori|addi) r(\d+),r(\d+),\s*(-?\d+)\s*$')
MEM = re.compile(r'^(lwz|lwzu|stw|stwu|ld|std|lbz|stb|lhz|sth|lfs|stfs|lfd|stfd)\s+\w+,\s*(-?\d+)\(r(\d+)\)$')
OPCODES = {0x22: 'DRAW_INDX', 0x36: 'DRAW_INDX_2', 0x2B: 'IM_LOAD_IMMEDIATE',
           0x27: 'IM_LOAD', 0x2D: 'SET_CONSTANT', 0x3B: 'INVALIDATE_STATE',
           0x3C: 'WAIT_REG_MEM', 0x46: 'EVENT_WRITE', 0x5A: 'EVENT_WRITE_EXT',
           0x58: 'EVENT_WRITE_SHD', 0x3F: 'INDIRECT_BUFFER'}


def load_candidates(path: Path) -> set[int]:
    with path.open(encoding='utf-8-sig', newline='') as f:
        rows = list(csv.DictReader(f))
    result = {int(row['guest_address'], 16) for row in rows}
    if not result or len(result) != len(rows):
        raise ValueError('Missing or duplicate candidate addresses')
    return result


def index_generated(gen: Path, targets: set[int], extra_lo: int = 0, extra_hi: int = 0) -> tuple[dict, dict, int, int]:
    """Stream all guest functions; retain only candidates and their direct callers.

    loc_ labels anchor guest addresses because emitted basic blocks may be out
    of address order. This recognizes only explicit unconditional bl/b calls.
    """
    bodies: dict[int, list[tuple[int, str]]] = {}
    callers: dict[int, list[dict]] = defaultdict(list)
    files = sorted(gen.glob('*_recomp.*.cpp'))
    if not files:
        raise ValueError(f'No generated *_recomp.NUM.cpp files under {gen}')
    functions = 0
    for path in files:
        cur = None
        nxt = 0
        with path.open(encoding='utf-8', errors='replace') as f:
            for line in f:
                m = FUNC.match(line)
                if m:
                    cur = int(m.group(1), 16)
                    nxt = cur
                    functions += 1
                    if cur in targets or extra_lo <= cur < extra_hi:
                        if cur in bodies:
                            raise ValueError(f'Duplicate guest definition 0x{cur:08X}')
                        bodies[cur] = []
                    continue
                if cur is None:
                    continue
                m = LABEL.match(line)
                if m:
                    nxt = int(m.group(1), 16)
                    continue
                m = ASM.match(line)
                if not m:
                    continue
                asm = m.group(1).strip()
                address = nxt
                nxt += 4
                if cur in bodies:
                    bodies[cur].append((address, asm))
                b = BRANCH.match(asm)
                if b:
                    target = int(b.group(2), 16)
                    if (target in targets or extra_lo <= target < extra_hi) and target != cur:
                        callers[target].append({'site': f'0x{address:08X}',
                                                'caller_guest': f'0x{cur:08X}',
                                                'kind': b.group(1)})
    return bodies, callers, len(files), functions


def evidence_for(ins: list[tuple[int, str]]) -> dict:
    """Literal observations; never infer a C++ XDK method name from PM4 alone."""
    candidates = []
    device_offsets = []
    last_lis: dict[int, tuple[int, int]] = {}
    for addr, instr in ins:
        m = LIS.match(instr)
        if m:
            last_lis[int(m.group(1))] = (addr, (int(m.group(2)) & 0xFFFF) << 16)
        n = ORI.match(instr)
        if n:
            op, dst, src, v = n.group(1), int(n.group(2)), int(n.group(3)), int(n.group(4))
            prev = last_lis.get(src)
            if prev and 0 < addr - prev[0] <= 64:
                value = (prev[1] | (v & 0xFFFF)) if op == 'ori' else (prev[1] + v) & 0xFFFFFFFF
                if value >> 30 == 3:
                    opcode = (value >> 8) & 0x7F
                    if opcode in OPCODES:
                        candidates.append({'at': f'0x{addr:08X}',
                                           'immediate': f'0x{value:08X}',
                                           'opcode': OPCODES[opcode],
                                           'note': 'literal candidate only; verify packet write and call ABI'})
        mem = MEM.match(instr)
        if mem and int(mem.group(3)) == 3 and int(mem.group(2)) >= 0:
            device_offsets.append({'at': f'0x{addr:08X}', 'asm': instr,
                                   'note': 'direct r3 access; r3 is NOT proven to be D3D device'})
    return {'instruction_count': len(ins),
            'pm4_literal_candidates': candidates,
            'r3_direct_memory_accesses': device_offsets,
            'first_ppc_instructions': [{'at': f'0x{a:08X}', 'asm': s} for a, s in ins[:48]],
            'validated_d3d_method': None}


def audit(gen: Path, csv_path: Path, xex: Path, pinned_sha: str,
          extra_lo: int = 0, extra_hi: int = 0) -> dict:
    actual = hashlib.sha256(xex.read_bytes()).hexdigest()
    if actual.lower() != pinned_sha.lower():
        raise ValueError(f'XEX SHA mismatch: actual {actual}, pinned {pinned_sha}')
    targets = load_candidates(csv_path)
    if extra_lo < 0 or extra_hi < extra_lo:
        raise ValueError('Invalid exploratory region bounds')
    bodies, callers, files, funcs = index_generated(gen, targets, extra_lo, extra_hi)
    regional = {}
    for addr in sorted(bodies):
        if addr in targets:
            continue
        e = evidence_for(bodies[addr])
        if e['r3_direct_memory_accesses'] or e['pm4_literal_candidates']:
            e['first_ppc_instructions'] = e['first_ppc_instructions'][:12]
            regional[f'0x{addr:08X}'] = dict(e, direct_callers=sorted(
                callers.get(addr, []), key=lambda c: (c['caller_guest'], c['site'])))
    return {
        'schema': 'hulk_native_xdk_6534_evidence_v1',
        'xex_sha256': actual,
        'candidate_source': str(csv_path),
        'generated_files_scanned': files,
        'guest_function_definitions_scanned': funcs,
        'candidate_count': len(targets),
        'candidate_definitions_found': len(targets & bodies.keys()),
        'missing_guest_definitions': [f'0x{x:08X}' for x in sorted(targets - bodies.keys())],
        'exploratory_region': [f'0x{extra_lo:08X}', f'0x{extra_hi:08X}'],
        'regional_functions_with_r3_or_pm4_evidence': regional,
        'limitations': [
            'PM4 constants do not establish Draw/Resolve/Present signatures',
            'r3 offsets are not DeviceLayout proof unless r3 is verified as device',
            'codegen PPC text alone does not prove runtime call frequency or frame content',
            'no inferred API addresses are injected into NativeXRenderer',
            'exploratory region is NOT a verified XDK library boundary'],
        'candidates': {f'0x{a:08X}': dict(evidence_for(bodies[a]),
                        direct_callers=sorted(callers.get(a, []), key=lambda x: (x['caller_guest'], x['site'])))
                       for a in sorted(targets & bodies.keys())}
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--generated', type=Path, required=True)
    ap.add_argument('--candidates', type=Path, required=True)
    ap.add_argument('--xex', type=Path, required=True)
    ap.add_argument('--pinned-sha256', required=True)
    ap.add_argument('--region-lo', type=lambda v: int(v, 0), default=0)
    ap.add_argument('--region-hi', type=lambda v: int(v, 0), default=0)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    report = audit(args.generated, args.candidates, args.xex, args.pinned_sha256,
                   args.region_lo, args.region_hi)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    stats = Counter(c['opcode'] for val in report['candidates'].values()
                    for c in val['pm4_literal_candidates'])
    print(f'[HULK/XDK] evidence only: {report["candidate_definitions_found"]}/'
          f'{report["candidate_count"]} candidate PPC bodies, '
          f'{report["generated_files_scanned"]} generated files; PM4 literals: {dict(stats)}; '
          f'report {args.output}')
    if report['missing_guest_definitions']:
        print(f'[HULK/XDK] WARNING: unmapped candidate definitions: '
              f'{report["missing_guest_definitions"][:12]}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
