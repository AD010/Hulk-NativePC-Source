#!/usr/bin/env python3
"""Source-only and C++ regression for upstream D139's safely backported shader memo."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from patch_native_x_hulk_d139_shader_memo import (
    patch, HEADER_OLD, HEADER_NEW, RESET_OLD, RESET_NEW,
    HASH_OLD, HASH_NEW, HIT_OLD, HIT_NEW, TAIL_OLD, TAIL_NEW,
)


def synthetic_source():
    return {
        "src/state.h": "// HULK/PS-RECENT-LOAD\n" + HEADER_OLD,
        "src/state.cpp": (
            "// HULK/PS-RECENT-LOAD\n// HULK/XDK-OWNED-IM-LOAD\n"
            + RESET_OLD + "\n"
            + "  for (const auto& load : scan_.shader_loads) {\n"
            + HASH_OLD + "\n" + HIT_OLD + "\n" + TAIL_OLD
        ),
    }


class D139ShaderMemoTests(unittest.TestCase):
    def test_atomic_idempotence_and_essential_ownership(self):
        original = synthetic_source()
        converted, installed = patch(original)
        self.assertTrue(installed)
        self.assertEqual(patch(converted), (converted, False))
        for piece in (HEADER_NEW,):
            self.assertIn(piece, converted["src/state.h"])
        for piece in (RESET_NEW, HASH_NEW, HIT_NEW, TAIL_NEW):
            self.assertIn(piece, converted["src/state.cpp"])
        self.assertIn("!std::memcmp(", converted["src/state.cpp"])
        self.assertIn("load.immediate ? nullptr", converted["src/state.cpp"])
        self.assertIn("program->ucode.size() == load.dwords", converted["src/state.cpp"])
        self.assertIn("hulk_loaded_at_ = {}", converted["src/state.cpp"])
        self.assertLess(
            converted["src/state.cpp"].index("HULK/PS-RECENT-LOAD"),
            converted["src/state.cpp"].index("HULK/D139-OWNED-SHADER-MEMO"))

    def test_rejects_partial_or_unreviewed_patched_state(self):
        original = synthetic_source()
        with self.assertRaises(ValueError):
            patch({**original, "src/state.h": HEADER_NEW})
        with self.assertRaises(ValueError):
            patch({**original, "src/state.cpp": original["src/state.cpp"].replace(HASH_OLD, "other")})
        with self.assertRaises(ValueError):
            patch({**original, "src/state.cpp": original["src/state.cpp"].replace(
                "HULK/XDK-OWNED-IM-LOAD", "missing")})
        with self.assertRaises(ValueError):
            patch({"src/state.h": HEADER_OLD})

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"), "C++ compiler unavailable")
    def test_exact_production_fast_path_still_recognizes_guest_shader_mutation(self):
        # Compile the unchanged production fast-path C++ statement as emitted
        # by the patch (only the xxhash line is handled by a trivial counter).
        actual_fast_path = HASH_NEW.split("    uint64_t hash = XXH3_64bits(", 1)[0]
        self.assertIn("!std::memcmp(hulk_at->program->ucode.data(), ucode,", actual_fast_path)
        code = r"""
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cassert>
struct Program {std::vector<uint32_t> ucode;};
struct HulkLoadedAt {uint32_t address=0, dwords=0; const Program* program=nullptr;};
struct Load {bool immediate; uint32_t address,dwords,type;};
struct Tracker {
  std::array<HulkLoadedAt,256> hulk_loaded_at_{};
  std::array<const Program*,2> programs_{};
  unsigned hashes=0;
  void Scan(Load load,const uint32_t* ucode) {
    for (int once=0; once<1; ++once) {
""" + actual_fast_path + r"""
      ++hashes;
      continue;
    }
  }
};
int main() {
  Program original{{10,20,30,40}}, updated{{10,20,31,40}};
  Tracker t;
  constexpr uint32_t address=0xA0001400u;
  t.hulk_loaded_at_[((address >> 4) ^ (address >> 12)) & 255u] =
      {address,4,&original};
  const Load regular{false,address,4,1};
  t.Scan(regular,original.ucode.data());
  assert(t.hashes==0 && t.programs_[1]==&original);
  t.Scan(regular,updated.ucode.data());
  assert(t.hashes==1); // same address, mutated shader
  t.Scan({true,address,4,1},original.ucode.data());
  assert(t.hashes==2); // immediate IM_LOAD cannot reuse guest address
  t.Scan({false,address,3,1},original.ucode.data());
  assert(t.hashes==3); // byte length changed
  t.hulk_loaded_at_={};
  t.Scan(regular,original.ucode.data());
  assert(t.hashes==4); // resetting tracker clears memo
  t.hulk_loaded_at_[((address >> 4) ^ (address >> 12)) & 255u] =
      {address,0,&original};
  t.Scan({false,address,0,1},original.ucode.data());
  assert(t.hashes==5); // zero-length shader never goes through memcmp
}
"""
        with tempfile.TemporaryDirectory(prefix="hulk_d139_memo_") as d:
            src=Path(d)/"shader.cpp"
            exe=Path(d)/"shader"
            src.write_text(code,encoding="utf-8")
            compiler=shutil.which("clang++") or shutil.which("g++")
            subprocess.run([compiler,"-std=c++20","-O1","-Wall","-Wextra","-Werror",
                            str(src),"-o",str(exe)],check=True,capture_output=True,text=True)
            subprocess.run([str(exe)],check=True,capture_output=True,text=True)


if __name__=="__main__":
    unittest.main()
