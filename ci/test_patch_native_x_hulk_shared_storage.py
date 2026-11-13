#!/usr/bin/env python3
"""Check NativeX storage identity, source anchors, and C++ key matching."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import patch_native_x_hulk_shared_storage as p

class NativeSharedStorageTests(unittest.TestCase):
    def test_exact_anchor_and_idempotence(self):
        source = p.METHOD_ANCHOR + "\n" + p.BODY_OLD
        after, changed = p.patch_cpp(source)
        self.assertTrue(changed)
        self.assertEqual(p.patch_cpp(after), (after, False))
        self.assertIn("target.texture = sibling->texture", after)
        self.assertIn("target.stencil_srv = sibling->stencil_srv", after)
        self.assertIn("target.guest_width = s.width", after)
        self.assertIn("target.scale = view_scale", after)
        self.assertIn("std::max(it->second.written, sibling->written)", after)
        self.assertTrue(p.patch_header(p.HEADER_OLD)[1])
        self.assertEqual(p.patch_header(p.HEADER_NEW), (p.HEADER_NEW, False))

    def test_d140_recent_target_memo_cannot_return_stale_sibling(self):
        # D139's recent_ fast path bypasses the targets_ map entirely.
        # Shared EDRAM views MUST synchronize write epochs in both paths.
        source = p.METHOD_ANCHOR + "\n" + p.BODY_D140_OLD
        patched, changed = p.patch_cpp(source)
        self.assertTrue(changed)
        self.assertEqual(p.patch_cpp(patched), (patched, False))
        self.assertIn("HULK/D139-RECENT-SHARED-EPOCH", patched)
        self.assertIn("for (const Recent& recent : recent_)", patched)
        self.assertIn("std::max(recent.target->written, sibling->written)", patched)
        self.assertIn("std::max(it->second.written, sibling->written)", patched)
        self.assertIn("return remember(&target);", patched)
        self.assertIn("target.native = false;", patched)
        self.assertNotIn("return recent.target;\n    }", patched)
        # The synthetic descriptor's alias identity is unchanged:
        # neither a reduced/shadow/native view nor a different EDRAM base
        # may be linked only because its host dimensions match.
        self.assertIn("kind != Kind::kRender", patched)
        self.assertIn("s.edram_base()", patched)
        with self.assertRaises(ValueError):
            p.patch_cpp(p.METHOD_ANCHOR + "\n" +
                        p.BODY_D140_NEW.replace(
                            "HULK/D139-RECENT-SHARED-EPOCH", "bad-marker"))

    def test_compiled_cpp_identity_color_and_depth(self):
        compiler = next((x for x in ("clang++","g++","c++") if shutil.which(x)), None)
        if not compiler:
            self.skipTest("C++ compiler unavailable")
        method = p.METHOD_NEW.removesuffix(p.METHOD_ANCHOR)
        fixture = r"""
#include <map>
#include <tuple>
#include <cstdint>
#include <algorithm>
#include <cassert>
struct GuestSurface {
 bool depth=false,valid=true; uint32_t width=0,height=0,base=0,rowpitch=0,aa=0,fmt=0;
 explicit operator bool() const {return valid;}
 uint32_t msaa() const {return aa;}
 uint32_t pitch() const {return rowpitch;}
 uint32_t edram_base() const {return base;}
};
struct RenderScale {
 float x=2,y=2;
 uint32_t HostWidth(uint32_t v)const{return uint32_t(v*x);}
 uint32_t HostHeight(uint32_t v)const{return uint32_t(v*y);}
};
struct HostTarget {
 bool texture=true,view=true,srv=true,depth=false;
 uint32_t width=2560,height=1440,samples=1; uint64_t written=0,last_used=0;
};
struct RenderTargets {
 enum class Kind {kRender,kReduced,kShadow};
 using SurfaceTuple=std::tuple<bool,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t>;
 using Key=std::tuple<SurfaceTuple,Kind>;
 bool hulk_sample_grid_=true; uint32_t hulk_output_width_=1280,hulk_output_height_=720;
 RenderScale scale_;
 std::map<Key,HostTarget> targets_;
 static SurfaceTuple SurfaceKey(const GuestSurface& s) {
    return {s.depth,s.base,s.rowpitch,s.aa,s.fmt,s.width,s.height};
 }
 HostTarget* FindHulkSharedStorage(const GuestSurface& s, Kind kind);
};
""" + method + r"""
int main() {
 RenderTargets rt;
 GuestSurface one{false,true,1280,720,1024,1280,0,0};
 GuestSurface four{false,true,640,360,1024,640,2,0};
 auto k=RenderTargets::Key{RenderTargets::SurfaceKey(four),RenderTargets::Kind::kRender};
 rt.targets_[k]=HostTarget{};
 assert(rt.FindHulkSharedStorage(one,RenderTargets::Kind::kRender)==&rt.targets_[k]);
 auto v=one;v.base=2048; assert(!rt.FindHulkSharedStorage(v,RenderTargets::Kind::kRender));
 v=one;v.rowpitch=1279; assert(!rt.FindHulkSharedStorage(v,RenderTargets::Kind::kRender));
 v=one;v.width=640; assert(!rt.FindHulkSharedStorage(v,RenderTargets::Kind::kRender));
 v=one;v.fmt=1; assert(!rt.FindHulkSharedStorage(v,RenderTargets::Kind::kRender));
 v=one;v.aa=1; assert(!rt.FindHulkSharedStorage(v,RenderTargets::Kind::kRender));
 assert(!rt.FindHulkSharedStorage(one,RenderTargets::Kind::kShadow));
 assert(!rt.FindHulkSharedStorage(one,RenderTargets::Kind::kReduced));
 rt.targets_[k].width=1280; assert(!rt.FindHulkSharedStorage(one,RenderTargets::Kind::kRender));
 rt.targets_[k].width=2560;rt.targets_[k].srv=false;
 assert(!rt.FindHulkSharedStorage(one,RenderTargets::Kind::kRender));
 rt.targets_.clear();
 k=RenderTargets::Key{RenderTargets::SurfaceKey(one),RenderTargets::Kind::kRender};
 rt.targets_[k]=HostTarget{};
 assert(rt.FindHulkSharedStorage(four,RenderTargets::Kind::kRender)==&rt.targets_[k]);
 rt.targets_.clear();
 GuestSurface d1{true,true,1280,720,0,1280,0,0};
 GuestSurface d4{true,true,640,360,0,640,2,0};
 k=RenderTargets::Key{RenderTargets::SurfaceKey(d4),RenderTargets::Kind::kRender};
 HostTarget depth;depth.depth=true;rt.targets_[k]=depth;
 assert(rt.FindHulkSharedStorage(d1,RenderTargets::Kind::kRender)==&rt.targets_[k]);
 d1.depth=false;assert(!rt.FindHulkSharedStorage(d1,RenderTargets::Kind::kRender));
 rt.hulk_sample_grid_=false;
 assert(!rt.FindHulkSharedStorage(d4,RenderTargets::Kind::kRender));
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "test.cpp"
            program = Path(tmp) / ("test.exe" if os.name == "nt" else "test")
            source.write_text(fixture, encoding="utf-8")
            subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror",
                            str(source),"-o",str(program)],check=True,timeout=40)
            subprocess.run([str(program)],check=True,timeout=10)

if __name__ == "__main__":
    unittest.main()
