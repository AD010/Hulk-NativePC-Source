#!/usr/bin/env python3
"""Compiled regression for SDK shader CF EXEC span guard."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from patch_sdk_hulk_cf_bounds import (
    patch_text, ANCHOR, GUARD, INCLUDE_OLD, INCLUDE_NEW, REPLACEMENT,
    CF_SUCCESSOR_OLD, CF_SUCCESSOR_GUARD, CF_NEXT_OLD, CF_NEXT_NEW,
)

def fixture():
    return (INCLUDE_OLD
            + "void Shader::GatherExecInformation("
            + "const ParsedExecInstruction& instr) {\n"
            + ANCHOR + "\n}\n"
            + "if (!cf_memexport_info_.empty()) {\n"
            + CF_SUCCESSOR_OLD + "\n"
            + "    cf_memexport_info_[successor_cf_index];\n}\n"
            + CF_NEXT_OLD + "\n")

class HulkCfBoundsTests(unittest.TestCase):
    def test_guard_idempotence_and_negative_source_drift(self):
        output, changed=patch_text(fixture())
        self.assertTrue(changed)
        self.assertEqual(patch_text(output),(output, False))
        self.assertEqual(output.count("HULK/CF-BOUNDS"), 1)
        self.assertLess(output.index("HULK/CF-BOUNDS"),
                        output.index("instr.Disassemble("))
        for damaged in ("garbled", INCLUDE_NEW + fixture(),
                        fixture().replace(ANCHOR, "different source")):
            with self.assertRaises(ValueError):
                patch_text(damaged)

    def test_shader_cf_successors_are_checked_before_dereference(self):
        source, changed = patch_text(fixture())
        self.assertTrue(changed)
        self.assertIn("HULK/CF-SUCCESSOR-BOUNDS", source)
        self.assertLess(source.index("HULK/CF-SUCCESSOR-BOUNDS"),
                        source.index("cf_memexport_info_[successor_cf_index]"))
        self.assertIn("successor_cf_index >> 1", CF_SUCCESSOR_GUARD)
        self.assertIn("std::out_of_range", CF_SUCCESSOR_GUARD)
        # The old SDK also enqueued one-past-end from its final valid CF
        # instruction. Preserve the end-of-program memexport bookkeeping.
        self.assertIn("HULK/CF-LAST-SUCCESSOR", source)
        self.assertIn("successor_cf_index + 1 < (cf_pair_index_bound_ << 1)", source)
        self.assertIn("memexport_eM_potentially_written_before_end_", CF_NEXT_NEW)
        self.assertNotIn("if (successor_cf_index < (cf_pair_index_bound_ << 1))",
                         CF_NEXT_NEW)
        with self.assertRaises(ValueError):
            patch_text(fixture().replace(CF_SUCCESSOR_OLD, "unknown CF walker"))
        with self.assertRaises(ValueError):
            patch_text(source.replace("HULK/CF-SUCCESSOR-BOUNDS", "removed marker"))

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),
                         "A C++ compiler is required")
    def test_final_cf_instruction_marks_end_without_enqueue(self):
        pre = r"""
#include <cassert>
#include <cstdint>
#include <vector>
struct Graph {
 uint32_t cf_pair_index_bound_ = 3;
 void Check(uint32_t successor_cf_index) {
   bool next_instr_is_new_successor = true;
   std::vector<uint32_t> successor_stack;
   uint32_t eM_written_by_cf_instr = 4;
   uint32_t memexport_eM_potentially_written_before_end_ = 0;
"""
        post = r"""
   if (successor_cf_index < 5) {
     assert(successor_stack.size() == 1u);
     assert(successor_stack[0] == successor_cf_index + 1);
     assert(memexport_eM_potentially_written_before_end_ == 0u);
   } else {
     assert(successor_stack.empty());
     assert(memexport_eM_potentially_written_before_end_ == 4u);
   }
 }
};
int main() {
  Graph g;
  for (uint32_t i=0; i < 6; ++i) g.Check(i);
}
"""
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "last_cf.cpp"
            exe = Path(d) / ("last_cf.exe" if os.name == "nt" else "last_cf")
            path.write_text(pre + CF_NEXT_NEW + post, encoding="utf-8")
            compiler = shutil.which("clang++") or shutil.which("g++")
            subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra",
                            "-Werror", str(path), "-o", str(exe)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(exe)], check=True, capture_output=True, text=True)

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),
                         "A C++ compiler is required")
    def test_actual_successor_guard_checks_all_cf_indices(self):
        # Compile the actual injected C++ guard in a minimal CF graph,
        # including UINT32_MAX to catch overflow and unchecked targets.
        pre = r"""
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>
struct Graph {
  std::vector<uint32_t> cf_memexport_info_ = std::vector<uint32_t>(6);
  uint32_t cf_pair_index_bound_ = 3;
  void Check(uint32_t index) {
    std::vector<uint32_t> successor_stack{index};
    while (!successor_stack.empty()) {
"""
        # The production snippet's following assignment needs an SDK
        # ControlFlowMemExportInfo; test the exact guard prefix only.
        guard_prefix = CF_SUCCESSOR_GUARD.split("          ControlFlowMemExportInfo&")[0]
        post = r"""
    }
  }
};
int main() {
  Graph g;
  for (uint32_t idx = 0; idx < 9; ++idx) {
    bool rejected = false;
    try { g.Check(idx); }
    catch (const std::out_of_range&) { rejected = true; }
    assert(rejected == (idx >= 6));
  }
  for (auto bad : {UINT32_MAX, UINT32_MAX - 1u}) {
    bool rejected = false;
    try { g.Check(bad); }
    catch (const std::out_of_range&) { rejected = true; }
    assert(rejected);
  }
}
"""
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "successor.cpp"
            exe = Path(d) / ("successor.exe" if os.name == "nt" else "successor")
            path.write_text(pre + guard_prefix + post, encoding="utf-8")
            compiler = shutil.which("clang++") or shutil.which("g++")
            subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra",
                            "-Werror", str(path), "-o", str(exe)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(exe)], check=True, capture_output=True, text=True)

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),
                         "A C++ compiler is required")
    def test_actual_guard_compiles_and_checks_65538_exec_spans(self):
        pre = r"""
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
struct Exec { uint32_t instruction_address, instruction_count; };
struct Tester {
 std::vector<uint32_t> ucode_data_ = std::vector<uint32_t>(27);
 void Check(Exec instr) {
"""
        post = r"""
 }
};
int main() {
 Tester t;
 for (uint32_t addr=0; addr<256; ++addr)
  for (uint32_t count=0; count<256; ++count) {
   bool rejected=false;
   try { t.Check({addr,count}); }
   catch (const std::out_of_range&) { rejected=true; }
   assert(rejected == (addr>9 || count>9-addr));
  }
 for (uint32_t addr : {UINT32_MAX, UINT32_MAX-1u}) {
  bool rejected=false;
  try { t.Check({addr,1}); }
  catch (const std::out_of_range&) { rejected=true; }
  assert(rejected);
 }
}
"""
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/"cf.cpp"
            exe=Path(d)/("cf.exe" if os.name=="nt" else "cf")
            path.write_text(pre + GUARD + post,encoding="utf-8")
            compiler=shutil.which("clang++") or shutil.which("g++")
            subprocess.run([compiler,"-std=c++20","-Wall","-Wextra",
                            "-Werror",str(path),"-o",str(exe)],
                           check=True,capture_output=True,text=True)
            subprocess.run([str(exe)],check=True,capture_output=True,text=True)

if __name__ == "__main__":
    unittest.main()
