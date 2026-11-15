#!/usr/bin/env python3
"""Reject silent Xenos fetch deletion in the Hulk offline shader translator."""
import unittest
from patch_xenosrecomp_hulk_fetch import patch, INCLUDE, INCLUDE_FIXED, OLD, NEW


class NativeXenosFetchTests(unittest.TestCase):
    def test_rewrites_upstream_exactly_once(self):
        text = INCLUDE + '\n' + OLD
        fixed, changed = patch(text)
        self.assertTrue(changed)
        self.assertEqual(patch(fixed), (fixed, False))
        self.assertIn('HULK/XENOSRECOMP-UNSUPPORTED-FETCH', fixed)
        self.assertIn('FetchOpcode::GetTextureWeights', fixed)
        self.assertIn('throw std::runtime_error', fixed)
        self.assertNotIn(OLD, fixed)
        self.assertIn(INCLUDE_FIXED, fixed)

    def test_drift_or_half_patch_fails(self):
        for text in (OLD, INCLUDE, INCLUDE_FIXED + '\n' + OLD,
                     INCLUDE + '\n' + NEW, 'unrelated converter'):
            with self.assertRaises(ValueError):
                patch(text)


if __name__ == '__main__':
    unittest.main()
