#!/usr/bin/env python3
"""Synthetic runtime XDK-to-USA PPC callsite crosswalk tests; no game bytes."""
import unittest

from hulk_eor_xdk_callsite_crosswalk import analyze

HOOKS = {
    0x8288DF30: 'DrawVertices',
    0x8288E318: 'DrawIndexedVertices',
    0x82880778: 'Present',
}
REVERSE = {
    0x8288E318: [
        (0x82500000, 0x82500020),
        (0x82500080, 0x825000A0),
    ],
    0x8288DF30: [(0x82600000, 0x82600024)],
}


def trace(xdk='8288E318', lr='82500024', kind=1, draws=10, frames=2):
    return ('[HULK/EOR-XDK-CALLSITE] XDK=0x%s LR=0x%s kind=%d '
            'draws=%d frames=%d first=1 last=2 varying_r3_r10_mask=0xFF\n'
            % (xdk, lr, kind, draws, frames))


class EorRuntimeCallsiteTests(unittest.TestCase):
    def test_exact_return_address_is_static_bl_callsite_plus_four(self):
        result = analyze(trace(), HOOKS, REVERSE)
        self.assertEqual(result['matched_direct_callsite_count'], 1)
        self.assertEqual(result['unmatched_callsite_count'], 0)
        row = result['matched'][0]
        self.assertEqual(row['static_direct_bl_site'], '0x82500020')
        self.assertEqual(row['source_defined_guest_callers'], ['0x82500000'])
        self.assertTrue(row['verified_direct_bl_link'])
        self.assertFalse(row['verified_engine_draw_abi'])
        self.assertFalse(row['verified_camera_or_projection'])

    def test_unknown_return_address_stays_unmatched(self):
        result = analyze(trace(lr='82500030'), HOOKS, REVERSE)
        self.assertEqual(result['matched_direct_callsite_count'], 0)
        self.assertIsNone(result['unmatched'][0]['static_direct_bl_site'])
        self.assertEqual(result['unmatched'][0]['source_defined_guest_callers'], [])

    def test_multiple_frames_hold_latest_cumulative_count_only(self):
        result = analyze(trace(draws=5, frames=1) +
                         trace(draws=17, frames=3) +
                         trace(xdk='8288DF30', lr='82600028', kind=0,
                               draws=8, frames=2), HOOKS, REVERSE)
        self.assertEqual(result['matched_direct_callsite_count'], 2)
        self.assertEqual(result['matched'][1]['observed_calls'], 17)

    def test_bad_hook_stage_and_retrograde_logs_rejected(self):
        for source in [
            trace(kind=0),
            trace(xdk='82880778'),
            trace(lr='00000000'),
            trace(draws=0),
            trace(draws=2, frames=3),
            trace(draws=8, frames=2) + trace(draws=7, frames=3),
            '[HULK/EOR-XDK-PROVENANCE] frame=2 actual calls=90',
        ]:
            with self.subTest(source=source[:45]), self.assertRaises(ValueError):
                analyze(source, HOOKS, REVERSE)

    def test_malformed_tagged_observation_cannot_hide_behind_valid_sample(self):
        malformed = [
            '[HULK/EOR-XDK-CALLSITE] XDK=0x8288E318 LR=bad kind=1 draws=10 frames=2',
            '[HULK/EOR-XDK-CALLSITE] XDK=0x8288E318 LR=0x82500024 kind=1 draws=bad frames=2',
            '[HULK/EOR-XDK-CALLSITE] XDK=0x8288E318 LR=0x82500024 kind=1 draws=10',
        ]
        for row in malformed:
            with self.subTest(row=row), self.assertRaisesRegex(ValueError, 'Malformed'):
                analyze(trace() + row + '\n', HOOKS, REVERSE)
        # Untagged unrelated diagnostics remain safely ignored.
        self.assertEqual(analyze(trace() + 'unrelated GPU diagnostic\n', HOOKS, REVERSE)
                         ['matched_direct_callsite_count'], 1)

    def test_does_not_emit_raw_ppc_registers_or_guest_bytes(self):
        result = analyze(trace(), HOOKS, REVERSE)
        import json
        data = json.dumps(result)
        self.assertNotIn('varying_r3_r10_mask', data)
        self.assertNotIn('original_game_bytes', data)
        self.assertIn('verified_engine_draw_abi', data)


if __name__ == '__main__':
    unittest.main()
