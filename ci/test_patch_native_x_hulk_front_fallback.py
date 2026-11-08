#!/usr/bin/env python3
"""Source-compatibility and idempotence guard for NativeX Resolve fallback."""
import unittest
from patch_native_x_hulk_front_fallback import (
    CPP_ANCHOR, CPP_NEW, H_ANCHOR, H_NEW,
    R_CVAR, R_CVAR_NEW, R_LOOKUP, R_LOOKUP_NEW,
    R_LOG, R_LOG_NEW, R_COMPOSE, R_COMPOSE_NEW,
    patch_renderer, patch_targets_header, patch_targets_cpp,
)

class TestHulkFrontFallback(unittest.TestCase):
    def test_targets_header(self):
        got, changed = patch_targets_header(H_ANCHOR)
        self.assertTrue(changed)
        self.assertIn("FindNewestPresentable", got)
        self.assertEqual(patch_targets_header(got), (got, False))
    def test_targets_cpp(self):
        got, changed = patch_targets_cpp(CPP_ANCHOR)
        self.assertTrue(changed)
        self.assertIn("ApplyInvalidations();", got)
        self.assertIn("item.last_resolve", got)
        self.assertEqual(patch_targets_cpp(got), (got, False))
    def test_renderer(self):
        prefix = '  if (call == Call::kPresent) g_front_buffer = ctx.r4.u32;'
        baseline = "\n".join((prefix, R_CVAR, R_LOOKUP, R_LOG, R_COMPOSE))
        updated, changed = patch_renderer(baseline)
        self.assertTrue(changed)
        self.assertIn("FindNewestPresentable(", updated)
        self.assertIn("hulk_front_fallback", updated)
        self.assertIn("fallback={} selected={:#x}", updated)
        self.assertIn("present_swizzle", updated)
        self.assertEqual(patch_renderer(updated), (updated, False))
    def test_missing_front_capture_refused(self):
        with self.assertRaises(ValueError):
            patch_renderer("\n".join((R_CVAR, R_LOOKUP, R_LOG, R_COMPOSE)))
    def test_unknown_upstream_refused(self):
        with self.assertRaises(ValueError):
            patch_targets_cpp("changed upstream")
    def test_partial_renderer_patch_refused(self):
        with self.assertRaises(ValueError):
            patch_renderer("\n".join((
                '  if (call == Call::kPresent) g_front_buffer = ctx.r4.u32;',
                R_CVAR_NEW, R_LOOKUP, R_LOG, R_COMPOSE
            )))

if __name__ == "__main__":
    unittest.main()
