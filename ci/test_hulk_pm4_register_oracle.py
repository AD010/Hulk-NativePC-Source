#!/usr/bin/env python3
"""Synthetic-only PM4 tests; no game packets or assets in repo."""
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
