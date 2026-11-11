#!/usr/bin/env python3
"""Regression: a physical image's write sequence must beat stale guest views."""
from __future__ import annotations
import unittest
from patch_native_x_hulk_shared_write_order import (
    patch, HEADER_ANCHOR, MSAA_OLD, DEPTH_OLD, DEPTH_DECL_OLD, CPP_ANCHOR
)

def fixture() -> tuple[str,str]:
    return (
        HEADER_ANCHOR,
        "// FindHulkSharedStorage(const GuestSurface& s, Kind kind)\n"
        + CPP_ANCHOR + "\n" + MSAA_OLD + "\n"
        + DEPTH_DECL_OLD + "\n" + DEPTH_OLD
    )

def latest(views, target):
    physical, recorded = views[target]
    return max(value for phys, value in views.values() if phys == physical)

def choose(views, candidates, threshold=0):
    choices = [(latest(views, k), k) for k in candidates
               if latest(views, k) > threshold]
    return max(choices)[1] if choices else None

class SharedWriteOrderTests(unittest.TestCase):
    def test_atomic_install_and_idempotency(self):
        a,b = fixture()
        h,c,changed = patch(a,b)
        self.assertTrue(changed)
        self.assertEqual(patch(h,c),(h,c,False))
        self.assertIn("HULK/SHARED-RESOLVE-WRITE",c)
        self.assertIn("other.texture.RawPtr() != candidate.texture.RawPtr()",c)
        self.assertIn("if (!hulk_sample_grid_ || !candidate.texture)",c)
        self.assertEqual(c.count("HulkSharedLatestWrite(target)"),2)
        with self.assertRaises(ValueError):
            patch(h,b)
        with self.assertRaises(ValueError):
            patch("wrong upstream",b)

    def test_physically_shared_1x_and_4x_writes(self):
        # 4x and 1x descriptions share image A; an unrelated candidate is B.
        views={"a_1x":("A",1),"a_4x":("A",45),"b_1x":("B",30)}
        self.assertEqual(latest(views,"a_1x"),45)
        self.assertEqual(choose(views,["a_1x","b_1x"]), "a_1x")
        self.assertEqual(choose(views,["a_1x","b_1x"],40),"a_1x")
        self.assertIsNone(choose(views,["a_1x","b_1x"],45))
        self.assertEqual(latest(views,"b_1x"),30)
        views["a_1x"]=("A",51)
        self.assertEqual(latest(views,"a_4x"),51)
        # If they refer to different real Vulkan textures, never merge.
        views["a_4x"]=("C",70)
        self.assertEqual(latest(views,"a_1x"),51)

if __name__ == "__main__":
    unittest.main()
