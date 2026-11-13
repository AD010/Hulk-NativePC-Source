#!/usr/bin/env python3
"""Private-XEX-free regression for native PM4 memory read bounds."""
import unittest
from pathlib import Path
import shutil
import subprocess
import tempfile
from patch_native_x_hulk_guest_memory_bounds import (
    patch, LOAD_OLD, LOAD_NEW, SHADER_OLD, SHADER_NEW, INLINE_OLD, INLINE_NEW
)

def safe(address, dwords):
    return 0xA0000000 <= address < 0xC0000000 and (
        address + dwords * 4 <= 0xC0000000) and dwords > 0

class SourceMemoryBoundsTests(unittest.TestCase):
    def test_atomic_patch_idempotency_and_rejection(self):
        source = "// HULK/PFP-PREDICATE HULK/PM4-SHADER-BOUNDS\n" + LOAD_OLD + "\n" + INLINE_OLD + "\n" + SHADER_OLD
        installed, changed = patch(source)
        self.assertTrue(changed)
        self.assertEqual(patch(installed), (installed, False))
        self.assertIn("uint64_t(address) + uint64_t(size) * 4ull", installed)
        self.assertIn("uint64_t(address) + uint64_t(dwords) * 4ull", installed)
        self.assertIn("HULK/PM4-SHADER-INLINE-BOUNDS", installed)
        self.assertNotIn(INLINE_OLD, installed)
        self.assertLess(installed.index(INLINE_NEW), installed.index(SHADER_NEW))
        self.assertLess(SHADER_NEW.index("out.complete = false"),
                        SHADER_NEW.index("GuestPointer(base, address)"))
        self.assertLess(LOAD_NEW.index("out.complete = false"),
                        LOAD_NEW.index("out.loads.emplace_back("))
        with self.assertRaises(ValueError):
            patch("broken")
        with self.assertRaises(ValueError):
            patch(LOAD_NEW + "\n" + INLINE_OLD + "\n" + SHADER_OLD)
    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),
                         "C++ compiler unavailable")
    def test_compiled_immediate_shader_packet_bounds_exhaustively(self):
        # Compiles the exact NativeX fragment inserted by the production
        # patch. 128 payload sizes x 128 advertised lengths, twice for
        # immediate/non-immediate = 32768 cases (game bytes never needed).
        from patch_native_x_hulk_guest_memory_bounds import INLINE_NEW
        prefix = """
#include <cassert>
#include <cstdint>
struct Scan { bool complete = true; };
void Validate(bool immediate, uint32_t count, uint32_t dwords, Scan& out) {
"""
        suffix = """
}
int main() {
  for (uint32_t count=0; count<128; ++count) {
    for (uint32_t size=0; size<128; ++size) {
      Scan inline_packet;
      Validate(true, count, size, inline_packet);
      assert(inline_packet.complete == (count >= 2 && size <= count-2));
      Scan external_packet;
      Validate(false, count, size, external_packet);
      assert(external_packet.complete);
    }
  }
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cpp = root / "hulk_shader_packet_bounds.cpp"
            exe = root / "hulk_shader_packet_bounds"
            cpp.write_text(prefix + INLINE_NEW + suffix, encoding="utf-8")
            compiler = shutil.which("clang++") or shutil.which("g++")
            subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra",
                            "-Werror", str(cpp), "-o", str(exe)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(exe)], check=True, capture_output=True,
                           text=True)

    def test_guest_memory_edges(self):
        for address,words in ((0xA0000000,1),(0xBFFFFFFC,1),(0xBFFFFF94,27)):
            self.assertTrue(safe(address, words))
        for address,words in ((0x9FFFFFFC,1),(0xBFFFFFFC,2),
                              (0xC0000000,1),(0xA0000000,0),
                              (0xBFFF0000,65535)):
            self.assertFalse(safe(address, words))

if __name__ == "__main__":
    unittest.main()
