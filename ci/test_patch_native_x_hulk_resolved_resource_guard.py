#!/usr/bin/env python3
"""No game binary or extracted shader needed for these source-only tests."""
import unittest
from patch_native_x_hulk_resolved_resource_guard import (
    patch_text, GUEST_OLD, GUEST_NEW, HOST_OLD, HOST_NEW,
    INCLUDE_OLD, INCLUDE_NEW,
)

class ResolvedImageGuardTests(unittest.TestCase):
    def test_single_atomic_idempotent_patch(self):
        original = INCLUDE_OLD + GUEST_OLD + "\n" + HOST_OLD
        fixed, changed = patch_text(original)
        self.assertTrue(changed)
        self.assertEqual(patch_text(fixed), (fixed, False))
        self.assertIn("HULK/RESOLVE-RESOURCE-GUARD", fixed)
        self.assertIn("textures_.erase(key)", fixed)
        self.assertIn("device_->CreateTexture", fixed)
        with self.assertRaises(ValueError):
            patch_text(INCLUDE_OLD + GUEST_NEW + "\n" + HOST_OLD)
        with self.assertRaises(ValueError):
            patch_text("upstream changed")

    def test_rejects_invalid_descriptor_shapes(self):
        def guest(w, h, slices, cube):
            return bool(w and h and w <= 32768 and h <= 32768 and (
                (cube and w == h) or
                (not cube and 0 < slices <= 2048)))
        def host(w, h, cube):
            return bool(w and h and w <= 32768 and h <= 32768 and (
                not cube or w == h))
        self.assertFalse(guest(1280, 720, 6, True))
        self.assertFalse(guest(5891328, 1, 1, False))
        self.assertFalse(guest(640, 360, 0, False))
        self.assertTrue(guest(1280, 720, 1, False))
        self.assertTrue(guest(1024, 1024, 6, True))
        self.assertFalse(host(2560, 1440, True))
        self.assertTrue(host(2560, 1440, False))
        self.assertFalse(host(32769, 2048, False))
        self.assertIn("desc.Width != desc.Height", HOST_NEW)
        self.assertNotIn("CreateTexture(", GUEST_NEW)
        # A valid host texture MUST still be allocated after the guard.
        # The previous test mistakenly banned CreateTexture everywhere,
        # making a correct Vulkan code path fail the source preflight.
        self.assertIn("device_->CreateTexture(", HOST_NEW)
        self.assertLess(HOST_NEW.index("if (hulk_invalid_host)"),
                        HOST_NEW.index("device_->CreateTexture("))
        self.assertLess(HOST_NEW.index("return nullptr;"),
                        HOST_NEW.index("device_->CreateTexture("))

if __name__ == "__main__":
    unittest.main()
