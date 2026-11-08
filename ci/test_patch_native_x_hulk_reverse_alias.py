#!/usr/bin/env python3
"""Regression tests for Hulk's two Xenos surface descriptors sharing a native image."""
import unittest
from patch_native_x_hulk_reverse_alias import PATCHES, transform

class HulkReverseAliasTests(unittest.TestCase):
    def test_exact_anchors_idempotent_fail_closed(self):
        for relative, rules in PATCHES.items():
            with self.subTest(file=relative):
                raw = "\n".join(old for old,new in rules)
                after, modified = transform(raw, rules)
                self.assertTrue(modified)
                self.assertEqual(transform(after, rules), (after, False))
                with self.assertRaises(ValueError):
                    transform("unknown pinned NativeX source",rules)

    def test_storage_identity_and_write_order(self):
        cpp = "\n".join(new for old,new in PATCHES["src/targets.cpp"])
        self.assertIn("s.msaa() != 0",cpp)
        self.assertIn("std::get<1>(candidate) != s.edram_base()",cpp)
        self.assertIn("std::get<2>(candidate) * 2 != s.pitch()",cpp)
        self.assertIn("std::get<3>(candidate) != 2",cpp)
        self.assertIn("std::get<4>(candidate) != std::get<4>(desired)",cpp)
        self.assertIn("std::get<5>(candidate) * 2 != s.width",cpp)
        self.assertIn("std::get<6>(candidate) * 2 != s.height",cpp)
        self.assertIn("host.written > found->written",cpp)

    def test_native_color_resolve_pixel_mapping(self):
        cpp = "\n".join(new for old,new in PATCHES["src/renderer.cpp"])
        self.assertIn("alias->written > (source ? source->written : 0)",cpp)
        self.assertIn("from.width = (from.width + 1) / 2",cpp)
        self.assertIn("to.width = std::min(from.width * 2",cpp)
        self.assertIn("|| hulk_reverse_alias;",cpp)
        self.assertIn("[HULK/REVERSE-ALIAS]",cpp)
        self.assertIn("(hulk_reverse_alias || surface.msaa() == 2)",cpp)
        self.assertNotIn("surface.surface_info &=",cpp)
        self.assertNotIn("surface.width *= 2",cpp)

if __name__ == "__main__":
    unittest.main()
