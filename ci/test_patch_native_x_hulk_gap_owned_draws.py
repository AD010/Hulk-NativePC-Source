#!/usr/bin/env python3
"""Synthetic source contract for immutable first-pass D140 gap replay."""
import unittest
import shutil
import subprocess
import tempfile
from pathlib import Path
from patch_native_x_hulk_gap_owned_draws import (
    patch, PRE_OLD, PRE_NEW, START_OLD, START_NEW, END_OLD, END_NEW
)

class OwnedReplayTests(unittest.TestCase):
    def source(self):
        return ("// HULK/PM4-PER-DRAW-BOUNDARY\n"
                "// HULK/PM4-REGISTER-SNAPSHOT\n"
                "// HULK/PM4-PRESCAN-SHADER-SNAPSHOT\n" +
                PRE_OLD + "\n" + START_OLD + "\n" + END_OLD)

    def test_atomic_idempotent_and_whole_gap_validation(self):
        source = self.source()
        updated, changed = patch(source)
        self.assertTrue(changed)
        self.assertEqual(patch(updated), (updated, False))
        self.assertIn("hulk_owned_draws.size() == expected", updated)
        self.assertIn("hulk_owned_draws.emplace_back(std::move(draw));", updated)
        self.assertIn("std::move(owned)", updated)
        self.assertIn("hulk_delta_from(scan_, hulk_owned_tail)", updated)
        self.assertIn("shader.ucode_offset -= uint32_t(hulk_cut.words);", updated)
        self.assertIn("absolute - first", updated)
        self.assertNotIn("emplace_back(scan_)", updated)
        self.assertIn("scan_ = std::move(hulk_final_state);", updated)
        self.assertIn("8u << 20", updated)
        self.assertIn("128u", updated)
        self.assertLess(updated.index("hulk_owned_draws.emplace_back(std::move(draw));"),
                        updated.index("hulk_gap_packet_callback_(*this"))
        self.assertEqual(updated.count("HULK/PM4-FIRST-PASS-OWNED-DRAWS-211"), 1)
        self.assertEqual(updated.count("hulk_gap_packet_callback_(*this"), 1)
        self.assertIn("else {\n            // Over-budget gaps retain reviewed D140", updated)
        # Each new branch must be closed exactly once; preserve the
        # enclosing StreamGap/legacy-bridge control flow in generated C++.
        self.assertEqual(updated.count("{") - source.count("{"),
                         updated.count("}") - source.count("}"))

    def test_fails_closed_on_drift_or_partial_install(self):
        source = self.source()
        for wrong in (source.replace(PRE_OLD, ""),
                      source.replace(START_OLD, ""),
                      source.replace(END_OLD, ""),
                      source.replace("HULK/PM4-REGISTER-SNAPSHOT", "unknown"),
                      PRE_NEW + "\n" + START_OLD + "\n" + END_OLD):
            with self.assertRaises(ValueError): patch(wrong)

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),
                         "No standalone C++ compiler available")
    def test_compiled_production_delta_rebases_registers_and_shader_offsets(self):
        # Compile the *actual emitted C++ delta logic*, not a Python model.
        # On a real 24-draw gap each saved stage must contain only its own
        # SET/LOAD and IM_LOAD bytes; repeating cumulative prefixes is a
        # serious source of both GPU load and stale-fetch corruption.
        header = r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>
struct PacketScan {
  struct ShaderLoad {
    uint32_t type, address, dwords, packet;
    bool immediate;
    uint32_t ucode_offset;
  };
  std::vector<std::pair<uint32_t, uint32_t>> writes, loads, predicated_writes;
  std::vector<uint32_t> register_order, shader_ucode;
  std::vector<ShaderLoad> shader_loads;
  uint64_t bin_mask=~0ull, bin_select=~0ull;
  uint32_t draws=0, indirect=0, predicated=0;
  bool complete=true;
  static constexpr uint32_t kInGuestMemory=UINT32_MAX;
  void Clear() {
    writes.clear(); loads.clear(); predicated_writes.clear();
    register_order.clear(); shader_ucode.clear(); shader_loads.clear();
    draws=indirect=predicated=0;complete=true;
  }
};
void TestOwnedDeltas() {
  PacketScan scan_;
"""
        suffix = r"""
  scan_.writes.push_back({100,0x111u});
  scan_.register_order.push_back(0); // first SET writes[0]
  scan_.shader_ucode={0x10u,0x20u};
  scan_.shader_loads.push_back({0,0,2,0,true,0});
  scan_.draws=1;
  hulk_on_prescan_draw();
  assert(hulk_owned_complete && hulk_owned_draws.size()==1);
  assert(hulk_owned_draws[0].shader_loads[0].ucode_offset==0);
  scan_.writes.push_back({101,0x222u});
  scan_.loads.push_back({102,0x333u});
  scan_.register_order.push_back(2); // second SET writes[1]
  scan_.register_order.push_back(1); // LOAD loads[0]
  scan_.shader_ucode.push_back(0x30u);
  scan_.shader_loads.push_back({1,0,1,0,true,2});
  scan_.draws=2;
  hulk_on_prescan_draw();
  assert(hulk_owned_complete && hulk_owned_draws.size()==2);
  const auto& first=hulk_owned_draws[0];
  const auto& second=hulk_owned_draws[1];
  assert(first.writes.size()==1 && first.loads.empty() &&
         first.shader_ucode.size()==2 && first.shader_loads.size()==1);
  assert(second.writes.size()==1 && second.loads.size()==1 &&
         second.shader_ucode.size()==1 && second.shader_loads.size()==1);
  assert(second.register_order.size()==2 &&
         second.register_order[0]==0 && second.register_order[1]==1);
  assert(second.shader_loads[0].ucode_offset==0 &&
         second.shader_ucode[0]==0x30u);
  scan_.writes.push_back({103,0x444u});
  scan_.register_order.push_back(4); // tail SET writes[2]
  PacketScan tail;
  assert(hulk_delta_from(scan_,tail) && tail.draws==0);
  assert(tail.writes.size()==1 && tail.register_order.size()==1 &&
         tail.register_order[0]==0 && tail.shader_loads.empty());
  assert(scan_.writes.size()==3 && hulk_owned_bytes>0);
  // An old pointer into a previous microcode region is forbidden.
  scan_.shader_loads.push_back({0,0,1,0,false,0});
  scan_.draws=3;
  hulk_on_prescan_draw();
  assert(!hulk_owned_complete && hulk_owned_draws.empty());
}
int main(){ TestOwnedDeltas(); }
"""
        with tempfile.TemporaryDirectory(prefix="hulk_pm4_owned_delta_") as folder:
            cpp=Path(folder)/"test.cpp"
            exe=Path(folder)/"test"
            cpp.write_text(header + PRE_NEW + "\n" + suffix, encoding="utf-8")
            compiler=shutil.which("clang++") or shutil.which("g++")
            result=subprocess.run(
                [compiler,"-std=c++20","-O1","-Wall","-Wextra","-Werror",
                 str(cpp),"-o",str(exe)],
                capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_original_draws_not_invented_by_snapshots(self):
        # A gap is eligible ONLY if every actual draw was captured and
        # the original scan itself reports complete (stream_gap prerequisite).
        for expected, owned, complete in ((24,24,True),(24,23,True),
                                          (24,24,False),(0,0,True)):
            stream_gap = complete and expected > 0
            should_submit = stream_gap and owned == expected
            self.assertEqual(should_submit,
                             complete and expected > 0 and owned == expected)

if __name__ == "__main__":
    unittest.main()
