#!/usr/bin/env python3
"""Independent, read-only PM4 register oracle for Hulk Xenos command dumps.

Reads a raw *big-endian* PM4 command stream, NOT an XEX or game archive.
Only directly observed Type-0/Type-1 and unpredicated SET_CONSTANT are
fully reconstructed. LOAD_ALU_CONSTANT marks the affected registers unknown
unless the caller independently supplies a verified physical memory snapshot.
Indirect buffers, predicates, malformed packets and unrecognized side effects
are flagged as incomplete rather than guessed. Inspired by the independent
command-vs-shadow comparison method in Fable2RE (not its capture format).
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct

BASE = {0: 0x4000, 1: 0x4800, 2: 0x4900, 3: 0x4908, 4: 0x2000}
WATCH = tuple(range(0x2000, 0x2400)) + tuple(range(0x4000, 0x48c0))
MAX_WORDS = 4_000_000
MAX_DRAWS = 256


def replay(words: list[int], watch=WATCH, max_draws=MAX_DRAWS) -> dict:
    if len(words) > MAX_WORDS:
        raise ValueError('PM4 input exceeds maximum safe word count')
    watch = frozenset(watch)
    known: dict[int, int] = {}
    unresolved: set[int] = set()
    counters = {'type0': 0, 'type1': 0, 'type3': 0, 'unknown_loads': 0,
                'predicated_packets': 0, 'indirect_packets': 0, 'unrecognized_classes': 0,
                'shader_constant_packets': 0}
    warnings: list[str] = []
    captures: list[dict] = []
    # Independently track the loaded VS/PS identities at each real GPU draw.
    # External shader memory is unavailable to this oracle; never guess it.
    programs: dict[str, dict] = {}
    shader_loads_by_stage = {'vs': 0, 'ps': 0}
    p = 0

    def write(reg: int, value: int) -> None:
        if 0 <= reg < 0x5000 and reg in watch:
            known[reg] = value & 0xffffffff
            unresolved.discard(reg)

    def mark_unknown(start: int, count: int) -> None:
        for reg in range(start, min(start + count, 0x5000)):
            if reg in watch:
                known.pop(reg, None)
                unresolved.add(reg)

    while p < len(words):
        h = words[p]; p += 1
        if h == 0:  # Consistent with NativeX packet scanner.
            continue
        typ = h >> 30
        if typ == 0:
            n = ((h >> 16) & 0x3fff) + 1
            if p + n > len(words):
                warnings.append(f'truncated Type0 at word {p-1}'); break
            base = h & 0x7fff
            one = (h >> 15) & 1
            for i in range(n):
                write(base if one else base + i, words[p+i])
            p += n; counters['type0'] += 1
            continue
        if typ == 1:
            if p + 2 > len(words):
                warnings.append(f'truncated Type1 at word {p-1}'); break
            write(h & 0x7ff, words[p])
            write((h >> 11) & 0x7ff, words[p+1])
            p += 2; counters['type1'] += 1
            continue
        if typ == 2:  # Type2 no-op.
            continue
        op = (h >> 8) & 0x7f
        n = ((h >> 16) & 0x3fff) + 1
        if p + n > len(words):
            warnings.append(f'truncated Type3 {op:02x} at word {p-1}'); break
        payload = words[p:p+n]; p += n
        counters['type3'] += 1
        if h & 1:
            counters['predicated_packets'] += 1
            # BIN predicate state isn't available from this standalone stream.
            # Snapshot possible draws but never apply uncertain register writes.
            if op not in (0x22, 0x36):
                warnings.append(f'predicated state packet {op:02x}: result unknown')
                continue
        if op == 0x2d and n >= 2:
            desc = payload[0]; cls = (desc >> 16) & 0xff
            if cls not in BASE:
                counters['unrecognized_classes'] += 1
            else:
                for i, val in enumerate(payload[1:]):
                    write(BASE[cls] + (desc & 0x7ff) + i, val)
        elif op in (0x55, 0x56):
            # HULK/PM4-DIRECT-SHADER-CONSTANTS: NativeX ScanPackets uses
            # payload word 0 & 0xFFFF as the absolute starting GPU register.
            # Unlike SET_CONSTANT (0x2D), no constant-type base is added.
            if n < 2:
                warnings.append(f'truncated SET_SHADER_CONSTANTS {op:02x}')
                continue
            start = payload[0] & 0xFFFF
            if start >= 0x5000 or n - 1 > 0x5000 - start:
                warnings.append(f'out-of-range SET_SHADER_CONSTANTS {op:02x}')
                continue
            for offset, value in enumerate(payload[1:]):
                write(start + offset, value)
            counters['shader_constant_packets'] += 1
        elif op == 0x2f and n >= 3:
            desc = payload[1]; cls = (desc >> 16) & 0xff
            count = payload[2] & 0xfff
            if cls not in BASE:
                counters['unrecognized_classes'] += 1
            else:
                mark_unknown(BASE[cls] + (desc & 0x7ff), count)
                counters['unknown_loads'] += 1
        elif op in (0x37, 0x3f):
            counters['indirect_packets'] += 1
            warnings.append(f'indirect command buffer {op:02x} not followed')
        elif op in (0x22, 0x36):
            at = 1 if op == 0x22 else 0
            if n <= at:
                warnings.append(f'truncated DRAW_INDX {op:02x} header')
                continue
            initiator = payload[at]
            if len(captures) < max_draws:
                captures.append({
                    'packet_end_word': p,
                    'opcode': f'0x{op:02x}',
                    'initiator': f'0x{initiator:08x}',
                    'known_watched_registers': {f'0x{k:04x}': f'0x{v:08x}'
                                                 for k, v in sorted(known.items())},
                    'unresolved_watched_registers': [f'0x{k:04x}' for k in sorted(unresolved)],
                    'predicate_state_unknown': bool(h & 1),
                    'shader_programs': {stage: state.copy()
                                        for stage, state in programs.items()},
                })
        elif op in (0x27, 0x2b):
            # HULK/PM4-SHADER-ORACLE: NativeX uses type & 3 for stage
            # and start_size & 0xFFFF for word count. Inline bytecode
            # starts after those first two descriptor dwords.
            if n < 2:
                warnings.append(f'truncated shader load packet {op:02x}')
                continue
            kind, start_size = payload[:2]
            stage_id, words_count = kind & 3, start_size & 0xFFFF
            if stage_id not in (0, 1) or not words_count:
                warnings.append(f'unresolved shader stage/length {op:02x}')
                continue
            stage = 'vs' if stage_id == 0 else 'ps'
            if op == 0x2b:
                if words_count > n - 2:
                    warnings.append('truncated immediate shader microcode')
                    continue
                raw = b''.join(struct.pack('>I', word)
                               for word in payload[2:2 + words_count])
                programs[stage] = {
                    'source': 'immediate',
                    'dwords': words_count,
                    'sha256': hashlib.sha256(raw).hexdigest(),
                    'external_guest_address': None,
                }
            else:
                programs[stage] = {
                    'source': 'external_unresolved',
                    'dwords': words_count,
                    'sha256': None,
                    'external_guest_address':
                        f'0x{(0xA0000000 | (kind & 0x1FFFFFFC)):08x}',
                }
                warnings.append('external shader bytecode not available in PM4')
            shader_loads_by_stage[stage] += 1
        elif op in (0x2d, 0x2f):
            warnings.append(f'truncated constant write/load {op:02x}')
    return {'draw_snapshots': captures, 'packet_counters': counters,
            'shader_loads_by_stage': shader_loads_by_stage,
            'warnings': warnings, 'complete': not warnings,
            'limitations': 'External shaders, memory loads, indirect packets and BIN predicates are NOT authoritative; SHA256 differs from NativeX XXH3.'}


def read_words(path: Path) -> list[int]:
    raw = path.read_bytes()
    if len(raw) % 4:
        raise ValueError('PM4 raw stream must contain complete 32-bit words')
    if len(raw) > MAX_WORDS * 4:
        raise ValueError('PM4 input too large')
    return [v[0] for v in struct.iter_unpack('>I', raw)]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('command_buffer', type=Path, help='Big-endian raw PM4 dump from your own game')
    ap.add_argument('--output', type=Path, help='Write analysis locally, never upload raw game data')
    ap.add_argument('--max-draws', type=int, default=MAX_DRAWS)
    a = ap.parse_args()
    if not 1 <= a.max_draws <= MAX_DRAWS:
        ap.error('--max-draws must be 1..256')
    summary = replay(read_words(a.command_buffer), max_draws=a.max_draws)
    result = json.dumps(summary, indent=2) + '\n'
    if a.output:
        a.output.write_text(result, encoding='utf-8')
    print(result)

if __name__ == '__main__':
    main()
