#!/usr/bin/env python3
"""No game binary or extracted shader needed for these source-only tests."""
import unittest
import shutil
import subprocess
import tempfile
from pathlib import Path
from patch_native_x_hulk_resolved_resource_guard import (
    patch_text, GUEST_OLD, GUEST_NEW, HOST_OLD, HOST_NEW,
    INCLUDE_OLD, INCLUDE_NEW, LOOKUP_OLD, LOOKUP_NEW, patch_lookup,
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

    def test_lookup_patch_requires_both_overloads(self):
        original = LOOKUP_OLD + "\n" + LOOKUP_OLD
        fixed, changed = patch_lookup(original)
        self.assertTrue(changed)
        self.assertEqual(patch_lookup(fixed), (fixed, False))
        with self.assertRaises(ValueError):
            patch_lookup(LOOKUP_OLD)
        with self.assertRaises(ValueError):
            patch_lookup(LOOKUP_NEW + "\n" + LOOKUP_OLD)

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"), "No C++ compiler")
    def test_allocated_but_unwritten_image_is_not_a_resolve(self):
        prefix = """
#include <cstdint>
#include <cassert>
struct Texture { uint64_t last_resolve=0; bool invalidated=false; };
Texture* Lookup(Texture* newest) {
"""
        suffix = """
}
int main() {
  Texture allocated;
  if (Lookup(&allocated)) return 42; // old code exposes uninitialized image
  allocated.last_resolve=1;
  assert(Lookup(&allocated)==&allocated);
  allocated.invalidated=true;
  assert(Lookup(&allocated)==nullptr);
  assert(Lookup(nullptr)==nullptr);
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            compiler=shutil.which("clang++") or shutil.which("g++")
            for label, body, expected in [("old",LOOKUP_OLD,42),("new",LOOKUP_NEW,0)]:
                source=root/(label+".cpp"); exe=root/label
                source.write_text(prefix+body+suffix)
                subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror",str(source),"-o",str(exe)],check=True)
                self.assertEqual(subprocess.run([str(exe)]).returncode, expected)

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
