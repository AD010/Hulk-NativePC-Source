#!/usr/bin/env python3
"""Regression: a physical image's write sequence must beat stale guest views."""
from __future__ import annotations
import unittest
import shutil
import subprocess
import tempfile
from pathlib import Path
from patch_native_x_hulk_shared_write_order import (
    patch, patch_renderer, REVERSE_OLD, REVERSE_NEW, FORWARD_OLD, FORWARD_NEW,
    HEADER_ANCHOR, MSAA_OLD, DEPTH_OLD, DEPTH_DECL_OLD, CPP_ANCHOR
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

    def test_same_vulkan_image_is_never_reinterpreted_twice(self):
        # If the guest 1x and 4x views already alias one native ITexture,
        # reverse-Resolve must NOT halve coordinates a second time.
        cpp = "// HULK/REVERSE-ALIAS\\n" + REVERSE_OLD + "\n" + FORWARD_OLD
        fixed, changed = patch_renderer(cpp)
        self.assertTrue(changed)
        self.assertEqual(patch_renderer(fixed), (fixed, False))
        self.assertIn("alias->texture.RawPtr() != source->texture.RawPtr()", fixed)
        self.assertIn("HulkSharedLatestWrite(*alias)", fixed)
        self.assertIn("HulkSharedLatestWrite(*source)", fixed)
        self.assertNotIn(REVERSE_OLD, fixed)
        self.assertIn(REVERSE_NEW, fixed)
        self.assertIn(FORWARD_NEW, fixed)
        with self.assertRaises(ValueError):
            patch_renderer("// unrelated upstream renderer")

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"), "No C++ compiler")
    def test_forward_alias_uses_physical_image_and_latest_write(self):
        prefix = r"""
#include <cassert>
#include <cstdint>
struct Ref { int* p; int* RawPtr() const { return p; } };
struct Target { Ref texture; uint64_t written, physical_write; };
struct Targets {
  uint64_t HulkSharedLatestWrite(const Target& t) const { return t.physical_write; }
} g_targets;
bool Select(Target* alias, Target* source) {
"""
        suffix = r"""
  return aliased;
}
int main() {
  int image_a=0, image_b=0;
  Target source{{&image_a},1,50}, same{{&image_a},50,50};
  if (Select(&same,&source)) return 42; // same image must not be scaled again
  Target stale{{&image_b},30,30};
  assert(!Select(&stale,&source)); // newer than stale view, older than real image
  Target newer{{&image_b},2,60};
  assert(Select(&newer,&source));
  assert(!Select(nullptr,&source));
  assert(!Select(&source,&source));
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            compiler=shutil.which("clang++") or shutil.which("g++")
            for label, body, expected in [("old",FORWARD_OLD,42),("new",FORWARD_NEW,0)]:
                source=root/(label+".cpp"); exe=root/label
                source.write_text(prefix+body+suffix)
                subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror",str(source),"-o",str(exe)],check=True)
                self.assertEqual(subprocess.run([str(exe)]).returncode,expected)

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
