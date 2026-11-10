#!/usr/bin/env python3
"""Offline source-only parity checks for Hulk's PFP predicate patch."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from patch_native_x_hulk_bin_predication import transform, BIN_SWITCH

def fixture():
    cpp = '''        if ((header & 1) && (opcode == 0x22 || opcode == 0x36)) {
          ++out.predicated;
          ScanDraw(base, opcode, p, count, out);
        } else if (header & 1) {
          ++out.predicated;
          if (opcode == 0x2D) {
            out.predicated_writes.emplace_back(0, 0);
          }
        } else {
          switch (opcode) {
            case 0x2D:
              out.writes.emplace_back(0, 0);
              break;
            default:
              break;
          }
        }
        p += 4 * count;
              if (immediate && dwords > count - 2) dwords = count - 2;
              if (copy_microcode) {
                out.shader_loads.push_back(
  draws = indirect = predicated = 0;
  complete = true;
  inline_begin_ = {};
}
void StateTracker::Reset() {
  inline_begin_ = {};
}
'''
    cpp += '''    scan_.Clear();
    ScanPackets(base, last_hulk_segment_end_, p.begin,
                scan_, 0, false, true);
          scan_.Clear();
          uint32_t emitted = 0;
'''
    header = '''  // (register, value), in order: type-0/1 writes and SET_CONSTANT*, from the ring and from the indirect
  // buffers it calls. Bin-predicated packets (per tile) are not included.
  uint32_t draws = 0, indirect = 0, predicated = 0;
  bool complete = true;
'''
    return {"src/state.cpp": cpp, "src/state.h": header}

class TestHulkPfpPatch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = fixture()
        cls.patched = transform(cls.original)

    def test_is_idempotent(self):
        self.assertEqual(self.patched, transform(self.patched))

    def test_mask_and_select_start_like_xenia(self):
        self.assertIn("bin_mask = 0xFFFFFFFFull, bin_select = 0xFFFFFFFFull", self.patched["src/state.h"])
        self.assertIn("scan_.bin_mask = 0xFFFFFFFFull", self.patched["src/state.cpp"])
        self.assertIn("scan_.bin_select = 0xFFFFFFFFull", self.patched["src/state.cpp"])

    def test_all_six_bin_opcodes_present(self):
        for op in (0x50, 0x51, 0x60, 0x61, 0x62, 0x63):
            self.assertIn(f"case 0x{op:02X}:", BIN_SWITCH)

    def test_predicated_draws_and_constants_share_gate(self):
        s = self.patched["src/state.cpp"]
        self.assertIn("(out.bin_mask & out.bin_select) == 0", s)
        self.assertIn("++out.bin_rejected;", s)
        self.assertNotIn("out.predicated_writes.emplace_back", s)
        self.assertIn("out.writes.emplace_back", s)

    def test_gap_state_rewound_on_second_scan(self):
        s = self.patched["src/state.cpp"]
        self.assertIn("const uint64_t hulk_bin_mask_at_gap = scan_.bin_mask", s)
        self.assertIn("scan_.bin_mask = hulk_bin_mask_at_gap;", s)
        self.assertIn("scan_.bin_select = hulk_bin_select_at_gap;", s)
        self.assertIn("[HULK/PFP-BIN] predicated PM4 packets skipped", s)
        self.assertIn("if (!scan_.complete)", s)

    def test_immediate_shader_bounds_fail_closed(self):
        cpp = self.patched["src/state.cpp"]
        self.assertIn("HULK/PM4-SHADER-BOUNDS", cpp)
        self.assertIn("(start_size >> 16) != 0", cpp)
        self.assertIn("dwords > count - 2", cpp)
        self.assertIn("if (copy_microcode || immediate)", cpp)
        self.assertNotIn("if (immediate && dwords > count - 2) dwords = count - 2;", cpp)

    def test_clear_does_not_erase_bin_flags(self):
        s = self.patched["src/state.cpp"]
        self.assertIn("bin_rejected = 0;", s)
        self.assertEqual(s.count("scan_.bin_mask = 0xFFFFFFFFull"), 1)

    def test_partial_source_and_header_mismatch_fails_closed(self):
        cpp_only = dict(self.patched)
        cpp_only["src/state.h"] = self.original["src/state.h"]
        with self.assertRaisesRegex(ValueError, "header/source mismatch"):
            transform(cpp_only)
        hdr_only = dict(self.original)
        hdr_only["src/state.h"] = self.patched["src/state.h"]
        with self.assertRaisesRegex(ValueError, "header/source mismatch"):
            transform(hdr_only)

    def test_invalid_upstream_source_fails(self):
        orig = fixture()
        orig["src/state.cpp"] = orig["src/state.cpp"].replace("case 0x2D", "case 0x2F")
        with self.assertRaisesRegex(ValueError, "source drift"):
            transform(orig)

    @unittest.skipUnless(shutil.which("clang++"), "optional C++ fragment compiler unavailable")
    def test_actual_bin_opcode_cpp_compiles(self):
        code = '''#include <cstdint>
#include <cstring>
#include <cassert>
struct PacketScan { uint64_t bin_mask=0xFFFFFFFFull, bin_select=0xFFFFFFFFull; bool complete=true; };
uint32_t Load32(const uint8_t* p, uint32_t i) { uint32_t x; memcpy(&x,p+i,4); return x; }
void update(PacketScan& out, uint32_t opcode, uint32_t count, const uint8_t* base) {
 uint32_t p=0; switch(opcode) {
''' + BIN_SWITCH + '''default: break;
}}
int main() {
  PacketScan p;
  uint32_t pair[]={0x13,0x29};
  update(p,0x50,2,(uint8_t*)pair);
  assert(p.bin_mask == ((uint64_t(0x13)<<32)|0x29));
  uint32_t one[]={0x21};
  update(p,0x62,1,(uint8_t*)one);
  assert(p.bin_select == 0x21ull);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp)/"bin.cpp"
            dst = Path(tmp)/"bin.exe"
            src.write_text(code)
            subprocess.run([shutil.which("clang++"), "-std=c++20", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(dst)], check=True, capture_output=True)
            subprocess.run([str(dst)], check=True, capture_output=True)

if __name__ == '__main__':
    unittest.main(verbosity=2)
