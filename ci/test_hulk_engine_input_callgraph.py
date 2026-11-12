#!/usr/bin/env python3
"""Synthetic EoR graph tests; no private XEX or game code required."""
import unittest
from hulk_engine_input_callgraph import analyze_input_context, INPUT_POLL_ANCHOR


class TestEorInputDiscovery(unittest.TestCase):
    def test_direct_and_common_graphics_parent_preserve_guest_callsites(self):
        graph = {
            INPUT_POLL_ANCHOR: [(0x82401000, 0x82401028),
                                (0x82402000, 0x82402020)],
            0x82401000: [(0x82500000, 0x82500014)],
            0x82402000: [(0x82500000, 0x82500024)],
            0x82500000: [(0x82500000, 0x82500030)],
        }
        render = {"candidate_ancestors": [
            {"guest_function": "0x82500000", "hook_groups": ["draw", "present"]}]}
        res = analyze_input_context(graph, render, max_depth=3)
        self.assertEqual(res["candidate_count"], 3)
        self.assertEqual(len(res["direct_poll_callers"]), 2)
        lead = res["candidate_eor_update_or_menu_callers"][0]
        self.assertEqual(lead["guest_function"], "0x82500000")
        self.assertEqual(lead["render_ancestor_groups"], ["draw", "present"])
        self.assertEqual([x["call_site"] for x in lead["evidence_chain"]],
                         ["0x82500014", "0x82401028"])
        self.assertFalse(lead["validated_eor_menu_hook"])
        self.assertFalse(lead["validated_camera_layout"])

    def test_no_guessed_menu_or_camera_and_depth_bounds(self):
        for bad in (0, 6):
            with self.assertRaises(ValueError):
                analyze_input_context({}, {}, max_depth=bad)
        for bad in (0, 501):
            with self.assertRaises(ValueError):
                analyze_input_context({}, {}, limit=bad)
        self.assertEqual(analyze_input_context({}, {})["candidate_count"], 0)
        result = analyze_input_context(
            {INPUT_POLL_ANCHOR: [(0x82560000, 0x82560020)]}, {})
        self.assertIn("virtual bctrl", " ".join(result["limitations"]))
        self.assertFalse(any(x["validated_camera_layout"]
                             for x in result["direct_poll_callers"]))

    def test_rejected_unrelated_and_indirect_addresses(self):
        graph = {INPUT_POLL_ANCHOR: [(0x12345678, 0x12345680),
                                     (0x82400000, 0x82400024)]}
        res = analyze_input_context(graph, {})
        self.assertEqual([x["guest_function"] for x in res["direct_poll_callers"]],
                         ["0x82400000"])


if __name__ == "__main__":
    unittest.main()
