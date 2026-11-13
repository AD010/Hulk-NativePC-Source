#!/usr/bin/env python3
import unittest
import shutil
import subprocess
import tempfile
from pathlib import Path
from patch_native_x_hulk_verify_key import (
    OLD_BIND, OLD_LOG, NEW_BIND, NEW_LOG, patch_text,
    CONSTANTS_OLD, CONSTANTS_NEW, COVERAGE_REPLACEMENTS, patch_coverage)

class TestVerifyHotkey(unittest.TestCase):
    def test_one_time_and_idempotence(self):
        original = OLD_BIND + "\n" + OLD_LOG
        patched, change = patch_text(original)
        self.assertTrue(change)
        self.assertIn(NEW_BIND, patched)
        self.assertIn(NEW_LOG, patched)
        self.assertNotIn(OLD_BIND, patched)
        self.assertEqual(patch_text(patched), (patched, False))
    def test_mixed_must_refuse(self):
        with self.assertRaises(ValueError):
            patch_text(NEW_BIND + "\n" + OLD_LOG)


class TestObserverCoverage(unittest.TestCase):
    def test_complete_and_idempotent(self):
        original = '\n'.join(old for old, _ in COVERAGE_REPLACEMENTS)
        patched, changed = patch_coverage(original)
        self.assertTrue(changed)
        self.assertEqual(patch_coverage(patched), (patched, False))
        self.assertNotIn('fully match the rebuilt state', patched)
        with self.assertRaises(ValueError):
            patch_coverage(original.replace(CONSTANTS_OLD, CONSTANTS_NEW))

    @unittest.skipUnless(shutil.which('clang++') or shutil.which('g++'), 'C++ compiler missing')
    def test_unknown_usage_detects_real_register_divergence(self):
        # Compile the exact modified selection loop. The old loop hides the
        # disagreement at PS c17; the new loop finds it without SDK analysis.
        prefix = r"""
#include <array>
#include <cstdint>
#include <cassert>
struct Map { bool float_dynamic_addressing=false; uint64_t float_bitmap[4]{}; };
bool Bit(const uint64_t* b, uint32_t i) { return (b[i/64] >> (i%64)) & 1; }
int main() {
  std::array<uint32_t,512> cpu{}, gpu{};
  gpu[256+17]=1;
  unsigned compared=0, mismatches=0;
  auto compare = [&](const Map* map, uint32_t window) {
"""
        suffix = r"""
      ++compared;
      if(cpu[(window+c)%512] != gpu[(window+c)%512]) ++mismatches;
    }
  };
  compare(nullptr,256);
  if(compared!=256 || mismatches!=1) return 42;
  compared=mismatches=0;
  Map known; known.float_bitmap[0]=uint64_t(1)<<18;
  compare(&known,256);
  assert(compared==1 && mismatches==0); // unused c17 is excluded when known
  compared=mismatches=0;
  known.float_dynamic_addressing=true;
  compare(&known,256);
  assert(compared==256 && mismatches==1);
  compared=mismatches=0;
  gpu.fill(0); gpu[0]=1;
  compare(nullptr,511);
  assert(compared==256 && mismatches==1); // wrapped window
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            compiler=shutil.which('clang++') or shutil.which('g++')
            for label, selection, expected in [('old',CONSTANTS_OLD,42),('new',CONSTANTS_NEW,0)]:
                source=root/(label+'.cpp'); exe=root/label
                source.write_text(prefix+selection+suffix)
                subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True)
                self.assertEqual(subprocess.run([str(exe)]).returncode,expected)

if __name__ == "__main__":
    unittest.main()
