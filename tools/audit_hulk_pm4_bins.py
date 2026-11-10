#!/usr/bin/env python3
"""Read-only Xbox 360 PM4 BIN predicate reference used for Hulk native render audit.

This does NOT change the live renderer. It proves the missing PFP predicate
state in the current NativeX Hulk synthetic Draw scanner before integrating
a patch. Xenia source reference:
  src/xenia/gpu/command_processor.cc (ExecutePacketType3, BIN opcodes)
  src/xenia/gpu/xenos.h (opcode IDs)
Only synthetic packets for tests; no game, XEX, microcode or image assets.
Usage: python tools/audit_hulk_pm4_bins.py --test
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Mapping, Sequence
import unittest
import sys

FULL = (1 << 64) - 1
# Xenia command_processor.h initializes PFP selection to the LOW 32 bits,
# not all 64 bits. Preserve this for accurate offline parity checks.
DEFAULT_BIN = 0xFFFFFFFF
BIN = {0x50, 0x51, 0x60, 0x61, 0x62, 0x63}
DRAW = {0x22, 0x36}
INDIRECT = 0x3F


class BadPacket(ValueError):
    pass


@dataclass
class State:
    bin_select: int = DEFAULT_BIN
    bin_mask: int = DEFAULT_BIN
    draws_seen: int = 0
    draws_selected: int = 0
    draws_skipped: int = 0
    bin_updates: dict[int, int] = field(default_factory=dict)
    indirect_buffers: int = 0


def type3(op: int, words: Sequence[int], predicate: bool = False) -> list[int]:
    if not 1 <= len(words) <= 0x4000:
        raise BadPacket("invalid type3 payload size")
    return [0xC0000000 | ((len(words) - 1) << 16) | ((op & 0x7F) << 8) |
            int(predicate), *words]


def parse(words: Sequence[int], indirect: Mapping[int, Sequence[int]] | None = None,
          state: State | None = None, max_depth: int = 4) -> State:
    s = state if state is not None else State()
    indirect = indirect or {}
    active = set()
    visited = [0]

    def visit(block: Sequence[int], depth: int):
        if depth > max_depth:
            raise BadPacket("indirect buffer nesting exceeded")
        pos = 0
        while pos < len(block):
            visited[0] += 1
            if visited[0] > 200000:
                raise BadPacket("too many packets")
            header = block[pos] & 0xFFFFFFFF
            pos += 1
            if header == 0:
                continue
            typ = header >> 30
            if typ == 2:
                continue
            if typ == 0:
                n = ((header >> 16) & 0x3FFF) + 1
                if pos + n > len(block): raise BadPacket("short type0")
                pos += n
                continue
            if typ == 1:
                if pos + 2 > len(block): raise BadPacket("short type1")
                pos += 2
                continue
            op = (header >> 8) & 0x7F
            n = ((header >> 16) & 0x3FFF) + 1
            if pos + n > len(block): raise BadPacket("short type3")
            payload = block[pos:pos+n]
            pos += n
            if op in DRAW: s.draws_seen += 1
            if (header & 1) and (s.bin_mask & s.bin_select) == 0:
                if op in DRAW: s.draws_skipped += 1
                continue
            if op in BIN:
                if op in (0x50, 0x51):
                    if n != 2: raise BadPacket("expected 64-bit bin pair")
                    value = ((payload[0] << 32) | payload[1]) & FULL
                    if op == 0x50: s.bin_mask = value
                    else: s.bin_select = value
                else:
                    if n != 1: raise BadPacket("expected 32-bit bin word")
                    val = payload[0] & 0xFFFFFFFF
                    if op == 0x60: s.bin_mask = (s.bin_mask & 0xFFFFFFFF00000000) | val
                    if op == 0x61: s.bin_mask = (s.bin_mask & 0xFFFFFFFF) | (val << 32)
                    if op == 0x62: s.bin_select = (s.bin_select & 0xFFFFFFFF00000000) | val
                    if op == 0x63: s.bin_select = (s.bin_select & 0xFFFFFFFF) | (val << 32)
                s.bin_updates[op] = s.bin_updates.get(op, 0) + 1
            elif op == INDIRECT:
                if n < 2: raise BadPacket("short INDIRECT_BUFFER")
                addr = payload[0] & 0x3FFFFFFF
                count = payload[1] & 0xFFFFF
                if addr not in indirect or len(indirect[addr]) < count:
                    raise BadPacket("INDIRECT_BUFFER memory unavailable")
                if addr in active: raise BadPacket("cyclic INDIRECT_BUFFER")
                active.add(addr)
                s.indirect_buffers += 1
                try: visit(indirect[addr][:count], depth+1)
                finally: active.remove(addr)
            elif op in DRAW:
                start = int(op == 0x22)  # skip VIZ query token
                if n <= start: raise BadPacket("DRAW without initiator")
                initiator = payload[start]
                if ((initiator >> 6) & 3) == 0 and n < start + 3:
                    raise BadPacket("DMA DRAW missing BASE/SIZE")
                s.draws_selected += 1

    visit(words, 0)
    return s


def flatten(*sequences: Sequence[int]) -> list[int]:
    return [word for sequence in sequences for word in sequence]


class BinParityTest(unittest.TestCase):
    auto = type3(0x36, [0x1082], True)
    plain = type3(0x36, [0x1082], False)

    def test_initial_low_32_bits_selected(self):
        first = parse(self.auto)
        self.assertEqual(first.draws_selected, 1)
        self.assertEqual(first.bin_mask, 0xFFFFFFFF)
        self.assertEqual(first.bin_select, 0xFFFFFFFF)

    def test_zero_mask_rejects_predicated_only(self):
        s = parse(flatten(type3(0x50, [0, 0]), self.auto, self.plain))
        self.assertEqual((s.draws_seen, s.draws_selected, s.draws_skipped), (2,1,1))

    def test_select_mask_intersection(self):
        s = parse(flatten(type3(0x50, [0, 2]), type3(0x51, [0, 1]),
                          self.auto, type3(0x51, [0, 2]), self.auto))
        self.assertEqual((s.draws_selected, s.draws_skipped), (1,1))

    def test_all_six_opcode_variants(self):
        s = parse(flatten(type3(0x50, [0, 0]), type3(0x51, [0, 0]),
                          type3(0x60, [32]), type3(0x61, [0]),
                          type3(0x62, [32]), type3(0x63, [0]), self.auto))
        self.assertEqual(s.draws_selected, 1)
        self.assertEqual(set(s.bin_updates), BIN)

    def test_high_32_bits(self):
        s = parse(flatten(type3(0x50, [12, 0]), type3(0x51, [4, 0]), self.auto))
        self.assertEqual((s.draws_selected, s.bin_select), (1, 4 << 32))

    def test_nested_indirect_persists_selection(self):
        sub = flatten(type3(0x60, [32]), self.auto)
        s = parse(flatten(type3(0x50, [0,0]), type3(0x51, [0,32]),
                          type3(0x3F, [0x2000,len(sub)]), self.auto), {0x2000:sub})
        self.assertEqual((s.draws_selected, s.indirect_buffers), (2, 1))

    def test_predicated_indirect_is_skipped(self):
        sub = flatten(self.plain, self.plain)
        s = parse(flatten(type3(0x50, [0,0]),
                          type3(0x3F,[0x2000,len(sub)],True),self.plain), {0x2000:sub})
        self.assertEqual((s.draws_selected, s.indirect_buffers), (1,0))

    def test_predicated_bin_write_is_skipped(self):
        s = parse(flatten(type3(0x50,[0,0]),type3(0x60,[0xFFFFFFFF],True),self.auto))
        self.assertEqual((s.bin_mask, s.draws_selected), (0,0))

    def test_cross_segment_state(self):
        s = parse(type3(0x50,[0,0]))
        self.assertEqual(parse(self.auto,state=s).draws_selected,0)

    def test_dma_complete_and_truncated(self):
        self.assertEqual(parse(type3(0x36,[0x1002,0x1000,8])).draws_selected,1)
        with self.assertRaises(BadPacket): parse(type3(0x36,[0x1002,0x1000]))

    def test_bad_packet_and_cycle(self):
        with self.assertRaises(BadPacket): parse(self.auto[:-1])
        cyc = type3(0x3F,[0x2000,3])
        with self.assertRaises(BadPacket): parse(cyc,{0x2000:cyc})


if __name__ == "__main__":
    if sys.argv[1:] == ["--test"]:
        unittest.main(argv=[sys.argv[0]], verbosity=2)
    else:
        print("Audit-only PM4 bin decoder. Run with --test; no game assets used.")
