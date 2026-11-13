#!/usr/bin/env python3
"""Synthetic-only PM4 tests; no game packets or assets in repo."""
import hashlib
import random
import struct
import tempfile
from pathlib import Path
import unittest
from hulk_pm4_register_oracle import replay, read_words


def t3(op, values, pred=False):
    assert values
    return [0xc0000000 | ((len(values)-1) << 16) | (op << 8) | int(pred)] + values


class PM4OracleTests(unittest.TestCase):
    def test_fetch_register_written_then_removed_by_unknown_load(self):
        # Texture fetch class=1 -> base register 0x4800, descriptor slot 1.
        set1 = t3(0x2d, [(1 << 16) | 6, 0x44a00000, 0x44340000])
        draw = t3(0x36, [(3 << 16) | 0x40])
        first = replay(set1 + draw)['draw_snapshots'][0]
        self.assertEqual(first['known_watched_registers']['0x4806'], '0x44a00000')
        self.assertEqual(first['known_watched_registers']['0x4807'], '0x44340000')
        load = t3(0x2f, [0x01000000, (1 << 16) | 6, 2])
        last = replay(set1 + load + draw)['draw_snapshots'][0]
        self.assertNotIn('0x4806', last['known_watched_registers'])
        self.assertIn('0x4806', last['unresolved_watched_registers'])
        self.assertEqual(replay(set1 + load + draw)['packet_counters']['unknown_loads'], 1)

    def test_type0_and_indirect_fail_closed(self):
        t0 = [(1 << 16) | 0x2000, 0x1234, 0x5678]
        r = replay(t0 + t3(0x3f, [0x2000, 1]) + t3(0x36, [0x40]))
        self.assertEqual(r['draw_snapshots'][0]['known_watched_registers']['0x2001'], '0x00005678')
        self.assertFalse(r['complete'])
        self.assertEqual(r['packet_counters']['indirect_packets'], 1)

    def test_predicated_constant_never_assumed_executed(self):
        state = t3(0x2d, [(1 << 16) | 6, 0x44a00000], pred=True)
        r = replay(state + t3(0x36, [0x40], pred=True))
        # DRAW_INDX_2 writes its own VGT_DRAW_INITIATOR regardless of
        # whether an earlier *constant* packet was BIN-predicated.
        self.assertEqual(r['draw_snapshots'][0]['known_watched_registers'],
                         {'0x21fc': '0x00000040'})
        self.assertTrue(r['draw_snapshots'][0]['predicate_state_unknown'])
        self.assertFalse(r['complete'])

    def test_immediate_shader_identity_then_external_unknown(self):
        ps = t3(0x2b, [1, 2, 0x12345678, 0xabcdef00])
        vs = t3(0x2b, [0, 1, 0x10203040])
        draw = t3(0x36, [0x40])
        external = t3(0x27, [0x1001, 2])
        r = replay(ps + vs + draw + external + draw)
        self.assertEqual(r['shader_loads_by_stage'], {'vs': 1, 'ps': 2})
        first, second = r['draw_snapshots']
        expected = hashlib.sha256(struct.pack('>II', 0x12345678,
                                              0xabcdef00)).hexdigest()
        self.assertEqual(first['shader_programs']['ps']['sha256'], expected)
        self.assertEqual(first['shader_programs']['vs']['source'], 'immediate')
        self.assertIsNone(second['shader_programs']['ps']['sha256'])
        self.assertEqual(second['shader_programs']['ps']['source'],
                         'external_unresolved')
        self.assertEqual(second['shader_programs']['ps']['external_guest_address'],
                         '0xa0001000')
        self.assertEqual(first['shader_programs']['ps']['sha256'], expected)
        self.assertFalse(r['complete'])

    def test_predicated_shader_does_not_reuse_stale_known_hash(self):
        original = t3(0x2b, [1, 1, 0x10203040])
        conditional = t3(0x2b, [1, 1, 0xdeadbeef], pred=True)
        result = replay(original + conditional + t3(0x36, [0x40]))
        state = result['draw_snapshots'][0]['shader_programs']['ps']
        self.assertIsNone(state['sha256'])
        self.assertEqual(state['source'], 'predicated_unresolved')
        self.assertFalse(result['complete'])
        self.assertEqual(result['shader_loads_by_stage']['ps'], 1)

    def test_predicated_constant_invalidates_only_its_registers(self):
        known = t3(0x2d, [(1 << 16), 0x00000002, 0x00000002])
        changed = t3(0x56, [0x4800, 0x44a00000], pred=True)
        result = replay(known + changed + t3(0x36, [0x40]))
        draw = result['draw_snapshots'][0]
        self.assertNotIn('0x4800', draw['known_watched_registers'])
        self.assertIn('0x4800', draw['unresolved_watched_registers'])
        self.assertEqual(draw['known_watched_registers']['0x4801'],
                         '0x00000002')
        self.assertFalse(result['complete'])

    def test_shader_start_offset_or_invalid_immediate_stage_preserves_old(self):
        original = t3(0x2b, [1, 1, 0x01020304])
        errors = [
            t3(0x2b, [0x10001, 1, 0x99887766]),
            t3(0x2b, [1, 0x10001, 0x99887766]),
            t3(0x27, [0x1003, 1]),
        ]
        for broken in errors:
            result = replay(original + broken + t3(0x36, [0x40]))
            state = result['draw_snapshots'][0]['shader_programs']['ps']
            self.assertEqual(state['sha256'],
                             hashlib.sha256(struct.pack('>I', 0x01020304)).hexdigest())
            self.assertEqual(result['shader_loads_by_stage']['ps'], 1)
            self.assertFalse(result['complete'])

    def test_bad_immediate_does_not_clobber_previous_program(self):
        ps = t3(0x2b, [1, 1, 0x11223344])
        invalid = t3(0x2b, [1, 6, 0x12345678])
        r = replay(ps + invalid + t3(0x36, [0x40]))
        stage = r['draw_snapshots'][0]['shader_programs']['ps']
        self.assertEqual(stage['sha256'],
                         hashlib.sha256(struct.pack('>I', 0x11223344)).hexdigest())
        self.assertEqual(r['shader_loads_by_stage']['ps'], 1)
        self.assertIn('truncated immediate shader microcode', r['warnings'])
        self.assertFalse(r['complete'])

    def test_seeded_immediate_shader_fuzz_keeps_previous_on_corruption(self):
        # 4096 deterministic synthetic tests: real shader-stage bits,
        # advertised word count, payload length and bytecode fingerprints.
        rng = random.Random(0x2008c0de)
        for sample in range(4096):
            stage = sample & 1
            initial = t3(0x2b, [stage, 1, 0x01020304])
            available = rng.randrange(0, 25)
            advertised = rng.randrange(0, 32)
            words = [rng.getrandbits(32) for _ in range(available)]
            proposed = t3(0x2b, [stage, advertised] + words)
            r = replay(initial + proposed + t3(0x36, [0x40]))
            got = r['draw_snapshots'][0]['shader_programs']
            target = 'ps' if stage else 'vs'
            if 1 <= advertised <= available:
                expected = hashlib.sha256(
                    b''.join(struct.pack('>I', w) for w in words[:advertised])
                ).hexdigest()
                self.assertEqual(got[target]['sha256'], expected)
                self.assertEqual(r['shader_loads_by_stage'][target], 2)
            else:
                expected = hashlib.sha256(struct.pack('>I', 0x01020304)).hexdigest()
                self.assertEqual(got[target]['sha256'], expected)
                self.assertEqual(r['shader_loads_by_stage'][target], 1)
                self.assertFalse(r['complete'])

    def test_direct_shader_constants_in_vs_ps_float_and_fetch_windows(self):
        # Xenos Type3 opcodes 0x55/0x56 use absolute register indices.
        # Verify real SET/LOAD ordering and distinguish fetch vs floats.
        write_vs = t3(0x56, [0x4000, 0x44a00000, 0x44340000])
        write_ps = t3(0x55, [0x4400, 0x3f800000])
        fetch = t3(0x2d, [(1 << 16), 0x00000002])
        draw = t3(0x36, [0x40])
        state = replay(write_vs + write_ps + fetch + draw)
        regs = state['draw_snapshots'][0]['known_watched_registers']
        self.assertEqual(regs['0x4000'], '0x44a00000')
        self.assertEqual(regs['0x4001'], '0x44340000')
        self.assertEqual(regs['0x4400'], '0x3f800000')
        self.assertEqual(regs['0x4800'], '0x00000002')
        self.assertEqual(state['packet_counters']['shader_constant_packets'], 2)
        self.assertTrue(state['complete'])

    def test_shader_constant_packet_requires_payload_and_valid_address(self):
        invalid_short = t3(0x56, [0x4000])
        invalid_address = t3(0x55, [0x4fff, 0x1, 0x2])
        draw = t3(0x36, [0x40])
        audit = replay(invalid_short + invalid_address + draw)
        self.assertFalse(audit['complete'])
        self.assertEqual(audit['packet_counters']['shader_constant_packets'], 0)
        self.assertTrue(any('truncated SET_SHADER_CONSTANTS' in w
                            for w in audit['warnings']))
        self.assertTrue(any('out-of-range SET_SHADER_CONSTANTS' in w
                            for w in audit['warnings']))
        # A valid auto-index draw sets VGT_DRAW_INITIATOR. Neither invalid
        # SET_SHADER_CONSTANTS packet may populate any float/fetch slot.
        self.assertEqual(audit['draw_snapshots'][0]['known_watched_registers'],
                         {'0x21fc': '0x00000040'})

    def test_dma_packet_side_registers_and_payload_bounds(self):
        # DRAW_INDX_2 source_select=0 needs VGT initiator/base/size.
        valid = t3(0x36, [0, 0x12345, 0x60])
        result = replay(valid)
        self.assertTrue(result['complete'])
        packet = result['draw_snapshots'][0]['known_watched_registers']
        self.assertEqual(packet['0x21fc'], '0x00000000')
        self.assertEqual(packet['0x21fa'], '0x00012345')
        self.assertEqual(packet['0x21fb'], '0x00000060')
        invalid = replay(t3(0x36, [0]))
        self.assertFalse(invalid['complete'])
        self.assertEqual(invalid['draw_snapshots'], [])
        self.assertIn('truncated DMA DRAW_INDX 36', invalid['warnings'])
        # DRAW_INDX opcode 0x22 additionally carries a query dword.
        invalid_index = replay(t3(0x22, [0x123, 0]))
        self.assertFalse(invalid_index['complete'])
        self.assertEqual(invalid_index['draw_snapshots'], [])
        self.assertIn('truncated DMA DRAW_INDX 22', invalid_index['warnings'])

    def test_seeded_dma_payload_fuzz_across_both_draw_opcodes(self):
        # The existing Release preflight runs this exact deterministic loop.
        # It checks every source_select mode, queried DRAW_INDX offsets, and
        # missing initiator/base/size without modifying game drawing behavior.
        rng = random.Random(0x2008feed)
        for i in range(8192):
            opcode = 0x22 if i & 1 else 0x36
            at = 1 if opcode == 0x22 else 0
            source_select = rng.randrange(4)
            initiator = source_select << 6
            remaining = rng.randrange(5)
            payload = ([0x1234] if at else []) + [initiator] + [
                rng.getrandbits(32) for _ in range(remaining)
            ]
            if i % 17 == 0:
                payload = payload[:at]  # Missing initiator.
            valid = (len(payload) > at and
                     (source_select != 0 or len(payload) >= at + 3))
            audit = replay(t3(opcode, payload or [0x1234]))
            snapshots = audit['draw_snapshots']
            if valid:
                self.assertEqual(len(snapshots), 1)
                self.assertEqual(snapshots[0]['initiator'],
                                 f'0x{initiator:08x}')
                self.assertTrue(audit['complete'])
                regs = snapshots[0]['known_watched_registers']
                self.assertEqual('0x21fa' in regs, source_select == 0)
                self.assertEqual('0x21fb' in regs, source_select == 0)
            else:
                self.assertEqual(snapshots, [])
                self.assertFalse(audit['complete'])

    def test_bad_packet_and_binary_data(self):
        self.assertFalse(replay([0xc0012d00, 0])['complete'])
        self.assertEqual(replay([])['draw_snapshots'], [])
        with tempfile.TemporaryDirectory() as td:
            f = Path(td) / 'pm4.bin'
            f.write_bytes(struct.pack('>II', 0x2000, 0x1234))
            self.assertEqual(read_words(f), [0x2000, 0x1234])
            f.write_bytes(b'xxx')
            with self.assertRaises(ValueError):
                read_words(f)


if __name__ == '__main__':
    unittest.main()
