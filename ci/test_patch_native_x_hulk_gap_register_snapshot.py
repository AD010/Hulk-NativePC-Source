#!/usr/bin/env python3
"""Synthetic PM4 first-pass SET/LOAD ownership tests."""
import unittest
import shutil
import subprocess
import tempfile
from pathlib import Path
from patch_native_x_hulk_gap_register_snapshot import (
    change,HEAD_OLD,HEAD_NEW,DRAW_OLD,DRAW_NEW,
    TAIL_OLD,TAIL_NEW,DIAG_OLD,DIAG_NEW,
    FLOAT_SCAN_OLD,FLOAT_SCAN_NEW,FLOAT_GATHER_OLD,FLOAT_GATHER_NEW,
    BIN_MISMATCH_OLD,BIN_MISMATCH_NEW,
    PRESCAN_BOUNDARY_OLD,PRESCAN_BOUNDARY_NEW,
    REPLAY_BOUNDARY_OLD,REPLAY_BOUNDARY_NEW
)

def restore(first, second):
    # Each entry is (source_type, register, value) where source_type=0/1.
    if len(first)<len(second):return None
    for i,(kind,reg,_) in enumerate(second):
        if (kind,reg)!=first[i][:2]:return None
    return [(kind,reg,first[i][2]) for i,(kind,reg,_) in enumerate(second)]

class Pm4RegisterLifetimeTests(unittest.TestCase):
    def test_atomic_patch_and_idempotence(self):
        src=("HULK/PM4-PRESCAN-SHADER-SNAPSHOT\n"+
             HEAD_OLD+DRAW_OLD+TAIL_OLD+DIAG_OLD+
             FLOAT_SCAN_OLD+FLOAT_GATHER_OLD+BIN_MISMATCH_OLD+PRESCAN_BOUNDARY_OLD)
        fixed,did=change(src)
        self.assertTrue(did)
        self.assertEqual(change(fixed),(fixed,False))
        for fragment in (HEAD_NEW,DRAW_NEW,TAIL_NEW,DIAG_NEW,
                         FLOAT_SCAN_NEW,FLOAT_GATHER_NEW,
                         BIN_MISMATCH_NEW,PRESCAN_BOUNDARY_NEW,
                         REPLAY_BOUNDARY_NEW):
            self.assertIn(fragment,fixed)
        self.assertIn("hulk_register_cursor == hulk_prescan_register_order.size()",fixed)
        self.assertIn("prior.first != now.first",fixed)
        for wrong in ("upstream drift",HEAD_NEW+DRAW_OLD+TAIL_OLD):
            with self.assertRaises(ValueError):change(wrong)

    def test_replay_keeps_validated_pfp_bin_even_on_failure(self):
        # First pass wins even when second pass has fewer draws or fails
        # after consuming some BIN_MASK / BIN_SELECT commands.
        source=("HULK/PM4-PRESCAN-SHADER-SNAPSHOT\\n" +
                HEAD_OLD + DRAW_OLD + TAIL_OLD + DIAG_OLD +
                FLOAT_SCAN_OLD + FLOAT_GATHER_OLD + BIN_MISMATCH_OLD +
                PRESCAN_BOUNDARY_OLD)
        actual, changed = change(source)
        self.assertTrue(changed)
        self.assertIn("HULK/PM4-REPLAY-BIN-OWNER", actual)
        self.assertIn("hulk_prescan_bin_mask = scan_.bin_mask", actual)
        self.assertIn("hulk_prescan_bin_select = scan_.bin_select", actual)
        self.assertIn("emitted != expected || !hulk_bin_replay_match", actual)
        self.assertIn("scan_.bin_mask = hulk_prescan_bin_mask;", actual)
        self.assertIn("scan_.bin_select = hulk_prescan_bin_select;", actual)
        self.assertEqual(change(actual), (actual, False))
        self.assertLess(actual.index("hulk_prescan_bin_mask ="),
                        actual.index("const auto hulk_prescan_writes"))
        verified_final = (0x01234567, 0xAAAABBBB)
        poisoned_replay = (0x00110022, 0x33334444)
        for complete, emitted, expected in ((False,1,3),(True,2,3),
                                            (True,3,3)):
            restored = poisoned_replay
            replay_matched = restored == verified_final
            if not complete or emitted != expected or not replay_matched:
                restored = verified_final
            self.assertEqual(restored, verified_final)

    def test_per_draw_boundaries_reject_missing_shader_or_registers(self):
        source=("HULK/PM4-PRESCAN-SHADER-SNAPSHOT\\n" +
                HEAD_OLD + DRAW_OLD + TAIL_OLD + DIAG_OLD +
                FLOAT_SCAN_OLD + FLOAT_GATHER_OLD + BIN_MISMATCH_OLD +
                PRESCAN_BOUNDARY_OLD)
        patched, changed = change(source)
        self.assertTrue(changed)
        self.assertEqual(change(patched), (patched, False))
        self.assertIn("HULK/PM4-PER-DRAW-BOUNDARY", patched)
        self.assertIn("hulk_prescan_draw_ends.emplace_back", patched)
        self.assertIn("hulk_prescan_draw_ends[emitted].first - hulk_register_cursor", patched)
        self.assertIn("hulk_prescan_draw_ends[emitted].second - hulk_prescan_cursor", patched)
        self.assertIn("hulk_shader_replay_ok = false;", patched)
        # The old gate checked ONLY the total by the end of the replay.
        # It could already emit a Draw after dropping its B SET or IM_LOAD.
        verified = [(3, 1), (5, 2)]
        def match(i, done_registers, draw_registers, done_shaders, draw_shaders):
            return (i < len(verified) and
                    (done_registers + draw_registers,
                     done_shaders + draw_shaders) == verified[i])
        self.assertTrue(match(0,0,3,0,1))
        self.assertFalse(match(0,0,2,0,1))
        self.assertFalse(match(0,0,3,0,0))
        self.assertTrue(match(1,3,2,1,1))
        self.assertFalse(match(1,3,2,1,0))
        self.assertFalse(match(2,5,0,2,0))

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),
                         "Standalone C++ compiler unavailable")
    def test_exact_cpp_per_draw_boundary_guard(self):
        # Compile the production C++ branch, rather than just duplicating
        # its intent in a Python model. A missing IM_LOAD or one missing
        # register must fail BEFORE the draw is published.
        guard = REPLAY_BOUNDARY_NEW.split(
            "            if (!hulk_restore_shader_words()", 1)[0]
        self.assertIn("hulk_prescan_draw_ends[emitted].first - hulk_register_cursor", guard)
        self.assertIn("hulk_prescan_draw_ends[emitted].second - hulk_prescan_cursor", guard)
        code = r"""
#include <vector>
#include <utility>
#include <cassert>
#include <cstddef>
#include <cstdint>
struct Packet {std::vector<unsigned> register_order, shader_loads;};
int main() {
  const auto attempt = [&](size_t emitted, size_t reg_cursor, size_t regs,
                           size_t shader_cursor, size_t shaders) {
    Packet scan_;
    scan_.register_order.resize(regs);
    scan_.shader_loads.resize(shaders);
    const std::vector<std::pair<size_t,size_t>> hulk_prescan_draw_ends{
        {3,1},{5,2}};
    size_t hulk_register_cursor=reg_cursor;
    size_t hulk_prescan_cursor=shader_cursor;
    bool hulk_shader_replay_ok=true, accepted=false;
    const auto on_draw = [&]() {
""" + guard + r"""
      accepted=true;
    };
    on_draw();
    return std::pair<bool,bool>{accepted, hulk_shader_replay_ok};
  };
  assert((attempt(0,0,3,0,1)==std::pair<bool,bool>{true,true}));
  assert((attempt(0,0,2,0,1)==std::pair<bool,bool>{false,false}));
  assert((attempt(0,0,3,0,0)==std::pair<bool,bool>{false,false}));
  assert((attempt(1,3,2,1,1)==std::pair<bool,bool>{true,true}));
  assert((attempt(1,3,2,1,0)==std::pair<bool,bool>{false,false}));
  assert((attempt(2,5,0,2,0)==std::pair<bool,bool>{false,false}));
  assert((attempt(0,SIZE_MAX,4,0,1)==std::pair<bool,bool>{false,false}));
  assert((attempt(0,0,3,SIZE_MAX,2)==std::pair<bool,bool>{false,false}));
}
"""
        with tempfile.TemporaryDirectory(prefix="hulk_draw_boundary_") as d:
            src=Path(d)/"boundary.cpp"
            exe=Path(d)/"boundary"
            src.write_text(code,encoding="utf-8")
            cxx=shutil.which("clang++") or shutil.which("g++")
            subprocess.run([cxx,"-std=c++20","-Wall","-Wextra",
                            "-Werror",str(src),"-o",str(exe)],
                           check=True,capture_output=True,text=True)
            subprocess.run([str(exe)],check=True,capture_output=True,text=True)

    def test_first_scan_owns_values_and_order(self):
        original=[(0,0x4806,0xDEADBEEF),(1,0x4807,0xAABBCCDD),
                  (0,0x2C10,0x00010004)]
        reused=[(0,0x4806,0x44A00000),(1,0x4807,0x44340000),
                (0,0x2C10,0xDEAD0000)]
        self.assertEqual(restore(original,reused),original)
        self.assertIsNone(restore(original,reused+[(0,0x100,7)]))
        self.assertIsNone(restore(original,[(1,0x4806,9)]))
        self.assertIsNone(restore(original,[(0,0x4807,9)]))
        self.assertEqual(restore(original,reused[:2]),original[:2])

    def test_xdk_draw_keeps_packet_float_constants(self):
        source=("HULK/PM4-PRESCAN-SHADER-SNAPSHOT\\n"+
                HEAD_OLD+DRAW_OLD+TAIL_OLD+DIAG_OLD+
                FLOAT_SCAN_OLD+FLOAT_GATHER_OLD+BIN_MISMATCH_OLD+PRESCAN_BOUNDARY_OLD)
        actual, did = change(source)
        self.assertTrue(did)
        self.assertIn("ScanPackets(base, p.scan_from, end, scan_, 0, false, true)", actual)
        self.assertIn("HULK/XDK-OWNED-IM-LOAD", actual)
        self.assertIn("!hulk_shadow_group_sent", actual)
        self.assertIn("value != shadow(hulk_shadow_byte)", actual)
        self.assertIn("d.ps_float_constants", actual)
        self.assertIn("d.vs_float_constants", actual)
        self.assertNotIn(FLOAT_SCAN_OLD, actual)
        self.assertNotIn(FLOAT_GATHER_OLD, actual)
        # XDK packet VS c21.x differs from the later shadow in #197.
        shadow=0xBF7F67F5
        packet=0xBF7FCBBA
        self.assertNotEqual(shadow, packet)
        resolved = packet if packet != shadow else shadow
        self.assertEqual(resolved, packet)
        with self.assertRaises(ValueError):
            change(source.replace(FLOAT_SCAN_OLD, "unreviewed scanner"))

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),
                         "Standalone C++ compiler unavailable")
    def test_real_float_packet_precedence_cpp(self):
        # Compile the exact production C++ branch, not a Python-only model.
        fragment=FLOAT_GATHER_NEW
        code=r"""
#include <cassert>
#include <cstdint>
#include <array>
constexpr uint32_t kFloatConstants=0x4000;
constexpr uint32_t kPixelConstantsDword=1024;
struct Device {uint32_t vs_float_constants=0,ps_float_constants=4096;};
int main(){
 Device d;
 std::array<uint32_t,2048> data{};
 struct Pending {uint64_t vs_dirty=0, ps_dirty=0;};
 Pending p;
 p.vs_dirty = UINT64_C(1) << (63u - 84u / 16u);
 p.ps_dirty = UINT64_C(1) << (63u - 84u / 16u);
 data[84]=0xBF7F67F5u;           // XDK VS c21.x device shadow
 data[(4096+84)/4]=0xBF7F67F5u;  // PS c21.x device shadow
 auto shadow=[&](uint32_t b){return data.at(b/4);};
 uint32_t emitted=0;
 uint32_t effective=0;
 auto SendValue=[&](uint32_t, uint32_t v){++emitted;effective=v;};
 const auto run=[&](uint32_t input_reg,uint32_t input_value,
                    bool input_draw,bool input_loaded) {
   uint32_t reg=input_reg,value=input_value;
   bool draw=input_draw,loaded=input_loaded;
""" + fragment + r"""
 };
 run(kFloatConstants+84,0xBF7FCBBAu,true,false);
 assert(emitted==1 && effective==0xBF7FCBBAu);
 run(kFloatConstants+84,0xBF7F67F5u,true,false);
 assert(emitted==1);
 // Dirty bit absent: even equality with shadow MUST emit packet state.
 p.vs_dirty = 0;
 run(kFloatConstants+84,0xBF7F67F5u,true,false);
 assert(emitted==2 && effective==0xBF7F67F5u);
 run(kFloatConstants+kPixelConstantsDword+84,0xBF7FCBBAu,true,false);
 assert(emitted==3 && effective==0xBF7FCBBAu);
 run(kFloatConstants+84,0xBF7F67F5u,true,true);
 assert(emitted==4 && effective==0xBF7F67F5u);
}
"""
        with tempfile.TemporaryDirectory(prefix="hulk_float_pm4_") as tmp:
            cpp=Path(tmp)/"float_packet.cpp"
            exe=Path(tmp)/"float_packet"
            cpp.write_text(code,encoding="utf8")
            compiler=shutil.which("clang++") or shutil.which("g++")
            subprocess.run([compiler,"-std=c++20","-Wall","-Wextra","-Werror",
                            str(cpp),"-o",str(exe)],
                           check=True,capture_output=True,text=True)
            subprocess.run([str(exe)],check=True,capture_output=True,text=True)

    def test_do_not_invent_texture_or_index_values(self):
        self.assertNotIn("skip_fetch",HEAD_NEW)
        self.assertNotIn("0x44A00000",HEAD_NEW)
        self.assertNotIn("0x44340000",HEAD_NEW)
        self.assertNotIn("std::memset",HEAD_NEW)

if __name__=="__main__":
    unittest.main()
