#!/usr/bin/env python3
"""Synthetic fail-closed tests for NativeX Vulkan unhooked single PM4 draw bridge."""
import unittest
from patch_native_x_hulk_gap_draw import PATCHES, transform

class GapDrawPatchTests(unittest.TestCase):
    def test_exact_all_sources(self):
        for path, rules in PATCHES.items():
            with self.subTest(path=path):
                src = "\n".join(old for old,new in rules)
                patched, changed = transform(src, rules)
                self.assertTrue(changed)
                self.assertIn("Hulk", patched) if "renderer" in path else None
                self.assertEqual(transform(patched, rules), (patched, False))

    def test_fail_closed_if_anchor_drift_or_partial(self):
        for rules in PATCHES.values():
            with self.assertRaises(ValueError):
                transform("upstream changed", rules)
            if len(rules) > 1:
                mixed = rules[0][1] + "\n" + "\n".join(old for old,new in rules[1:])
                with self.assertRaises(ValueError):
                    transform(mixed, rules)

    def test_never_fake_indirect_as_one_draw(self):
        state = dict(PATCHES["src/state.cpp"])
        joined = "\n".join(state.values())
        self.assertIn("scan_.draws == 1", joined)
        self.assertIn("scan_.indirect == 0", joined)
        self.assertIn("scan_.shader_loads.empty()", joined)

if __name__ == "__main__":
    unittest.main()
