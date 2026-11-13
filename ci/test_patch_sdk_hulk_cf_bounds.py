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
)

def fixture():
    return (INCLUDE_OLD
            + "void Shader::GatherExecInformation("
            + "const ParsedExecInstruction& instr) {\n"
            + ANCHOR + "\n}\n")

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
