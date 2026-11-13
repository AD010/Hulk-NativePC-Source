#!/usr/bin/env python3
"""Synthetic-only PM4 tests; no game packets or assets in repo."""
import hashlib
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
        draw = t3(0x36, [3 << 16])
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
        r = replay(t0 + t3(0x3f, [0x2000, 1]) + t3(0x36, [0]))
        self.assertEqual(r['draw_snapshots'][0]['known_watched_registers']['0x2001'], '0x00005678')
        self.assertFalse(r['complete'])
        self.assertEqual(r['packet_counters']['indirect_packets'], 1)

    def test_predicated_constant_never_assumed_executed(self):
        state = t3(0x2d, [(1 << 16) | 6, 0x44a00000], pred=True)
        r = replay(state + t3(0x36, [0], pred=True))
        self.assertEqual(r['draw_snapshots'][0]['known_watched_registers'], {})
        self.assertTrue(r['draw_snapshots'][0]['predicate_state_unknown'])
        self.assertFalse(r['complete'])

    def test_immediate_shader_identity_then_external_unknown(self):
        ps = t3(0x2b, [1, 2, 0x12345678, 0xabcdef00])
        vs = t3(0x2b, [0, 1, 0x10203040])
        draw = t3(0x36, [0])
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

    def test_bad_immediate_does_not_clobber_previous_program(self):
        ps = t3(0x2b, [1, 1, 0x11223344])
        invalid = t3(0x2b, [1, 6, 0x12345678])
        r = replay(ps + invalid + t3(0x36, [0]))
        stage = r['draw_snapshots'][0]['shader_programs']['ps']
        self.assertEqual(stage['sha256'],
                         hashlib.sha256(struct.pack('>I', 0x11223344)).hexdigest())
        self.assertEqual(r['shader_loads_by_stage']['ps'], 1)
        self.assertIn('truncated immediate shader microcode', r['warnings'])
        self.assertFalse(r['complete'])

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
