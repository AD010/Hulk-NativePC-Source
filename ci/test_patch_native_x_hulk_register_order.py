#!/usr/bin/env python3
"""Source-only regression for restoring Xenos PM4 SET/LOAD register order.

Compiles a small C++ model of the *actual helper* with clang++ when available.
Does not need Xbox game data, GPU, DiligentCore or Windows CI.
"""
from __future__ import annotations
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest
from patch_native_x_hulk_register_order import (
    transform, HELPERS, LAST_WRITE_OLD, OLD_LOAD_LOOP, GATHER_HEAD
)


def input_fixture() -> dict[str, str]:
    header = (
        "struct PacketScan {\n"
        "  std::vector<std::pair<uint32_t, uint32_t>> loads;\n"
        "};\n"
    )
    cpp = "/* HULK/PFP-PREDICATE HULK/PM4-PAYLOAD */\n"
    cpp += "void PacketScan::Clear(){\n  loads.clear();\n  shader_loads.clear();\n}\n"
    cpp += "bool LastWrite(){\n" + LAST_WRITE_OLD + "\n}\n"
    cpp += "// DRAW_INDX (viz query token, then as DRAW_INDX_2)\n"
    cpp += "void ScanDraw(){\n"
    cpp += "  out.writes.emplace_back(0, 1);\n" * 8
    cpp += "  out.loads.emplace_back(0, 2);\n}\n"
    cpp += "void Gather() {\n"
    cpp += "  for (const auto& [reg, value] : scan_.writes) {\n"
    cpp += "    if (reg >= kFloatConstants && reg < kFloatConstants + kFloatDwords) {\n"
    cpp += "      if (!draw) SendValue(reg, value);\n"
    cpp += "    } else { SendValue(reg,value); }\n"
    cpp += "  }\n"
    cpp += OLD_LOAD_LOOP
    cpp += "  for (const auto& load : scan_.shader_loads) {}\n}\n"
    return {"src/state.cpp":cpp, "src/state.h":header}


def effective_fetch(shadow: int, dirty: bool,
                    operations: list[tuple[str, int]], old: bool) -> int:
    # Faithful mini-model of Gather's fetch override and late dirty slot flush.
    if old:
        operations = [x for x in operations if x[0] == "set"] + [
            x for x in operations if x[0] == "load"]
    result = shadow
    overridden = False
    override = 0
    for source, value in operations:
        result = value
        if source == "set" or not old:
            overridden = value != shadow
            if overridden:
                override = value
    if dirty:
        result = shadow
    if overridden:
        result = override
    return result


class RegisterOrderTests(unittest.TestCase):
    def test_patch_and_idempotence(self):
        src = input_fixture()
        out = transform(src)
        self.assertEqual(transform(out),out)
        self.assertIn("std::vector<uint32_t> register_order",out["src/state.h"])
        self.assertIn("register_order.clear()",out["src/state.cpp"])
        self.assertEqual(out["src/state.cpp"].count("HulkOrderedWrite(out, "),8)
        self.assertEqual(out["src/state.cpp"].count("HulkOrderedLoad(out, "),1)
        self.assertIn("for (uint32_t hulk_tag : scan_.register_order)",out["src/state.cpp"])
        self.assertIn("if (!draw || loaded) SendValue",out["src/state.cpp"])
        self.assertNotIn(OLD_LOAD_LOOP,out["src/state.cpp"])

    def test_header_source_partial_rejected(self):
        src = transform(input_fixture())
        src["src/state.h"] = input_fixture()["src/state.h"]
        with self.assertRaisesRegex(ValueError,"Partial"):
            transform(src)

    def test_rejects_wrong_scanner_version(self):
        src = input_fixture()
        src["src/state.cpp"] = src["src/state.cpp"].replace(
            "out.loads.emplace_back(", "out.missing_emplace(")
        with self.assertRaisesRegex(ValueError,"anchors drift"):
            transform(src)

    def test_fetch_probe_names_real_source_set_or_load(self):
        original = input_fixture()
        opening = "  for (const auto& [reg, value] : scan_.writes) {\n"
        probe = (
            opening +
            '    REXLOG_WARN("[HULK/FETCH-WRITER] #{} call={} draw={} slot={} '
            'dword={} value={:#x} indirect={}",\n'
            '                    n, uint32_t(call), draw, (reg - kFetchConstants) / 6,\n'
            '                    (reg - kFetchConstants) % 6, value, scan_.indirect);\n'
        )
        self.assertIn(opening, original["src/state.cpp"])
        original["src/state.cpp"] = original["src/state.cpp"].replace(opening, probe, 1)
        converted = transform(original)["src/state.cpp"]
        self.assertIn("indirect={} source={}", converted)
        self.assertIn('loaded ? "LOAD_ALU" : "SET"', converted)

    def test_dirty_fetch_correctly_uses_latest_load(self):
        screen_width_float = 0x44A00000  # 1280.0 stored as raw bits
        valid_texture_fetch = 0x00010A2F
        ops=[("set",screen_width_float),("load",valid_texture_fetch)]
        self.assertEqual(effective_fetch(screen_width_float,True,ops,old=True),screen_width_float)
        self.assertEqual(effective_fetch(screen_width_float,True,ops,old=False),valid_texture_fetch)

    def test_reverse_packet_order_uses_last_set(self):
        old = 0x00010A2F
        latest = 0x44A00000
        self.assertEqual(effective_fetch(0x0,True,[("load",old),("set",latest)],old=False),latest)

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),"C++ compiler missing")
    def test_cxx_compiles_exact_ordering_helpers(self):
        helper = HELPERS.replace("HULK/PM4-REGISTER-ORDER","HULK/PM4-REGISTER-ORDER")
        cpp = r"""
#include <cstdint>
#include <vector>
#include <utility>
#include <cassert>
#include <iostream>
struct PacketScan {
 std::vector<std::pair<uint32_t,uint32_t>> writes, loads;
 std::vector<uint32_t> register_order;
 bool complete=true;
};
""" + helper + r"""
int main(){
 PacketScan p;
 HulkOrderedWrite(p,0x4806,0x44A00000);
 HulkOrderedLoad(p,0x4806,0x00010A2F);
 uint32_t latest=0;
 for(uint32_t tag:p.register_order){
   const auto& x=(tag&1)?p.loads[tag>>1]:p.writes[tag>>1];
   latest=x.second;
 }
 assert(latest==0x00010A2F);
 assert(p.register_order.size()==2);
 assert((p.register_order[0]&1)==0 && (p.register_order[1]&1)==1);
 std::cout<<"chronological PM4 SET/LOAD: PASS";
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)
            (path/"test.cpp").write_text(cpp,encoding="utf-8")
            exe=path/"test"
            compiler=shutil.which("clang++") or shutil.which("g++")
            subprocess.run([compiler,"-std=c++20","-Wall","-Wextra","-Werror",
                            str(path/"test.cpp"),"-o",str(exe)],check=True,
                           capture_output=True)
            subprocess.run([str(exe)],check=True,capture_output=True)


if __name__=="__main__":
    unittest.main(verbosity=2)
