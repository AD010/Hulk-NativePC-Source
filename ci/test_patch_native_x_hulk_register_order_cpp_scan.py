#!/usr/bin/env python3
"""Compile and execute the actual patched NativeX PM4 scanner and ordered SET/LOAD.

No shaders, game binaries, XEX, GPU or Windows runner needed.
"""
from __future__ import annotations
import subprocess
import sys
import tempfile
from pathlib import Path
from test_patch_native_x_hulk_bin_cpp_scan import PRELUDE

MAIN=r"""
int main() {
  std::vector<uint32_t> m(4096,0);
  const uint32_t address=0x1000;
  m[address/4]=0x00010A2Fu; // valid descriptor in synthetic guest memory
  auto emit=[&](uint32_t& off,uint32_t op,std::initializer_list<uint32_t> values) {
    m[off++]=0xC0000000u|((uint32_t(values.size())-1)<<16)|(op<<8);
    for(uint32_t v:values) m[off++]=v;
  };
  uint32_t off=0;
  emit(off,0x2D,{0x00010006u,0x44A00000u}); // SET fetch1 dword0 to 1280.0
  emit(off,0x2F,{address,0x00010006u,1u}); // LOAD actual descriptor after SET
  PacketScan p;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),0,off*4,p,0,false,true);
  assert(p.complete && p.writes.size()==1 && p.loads.size()==1);
  assert(p.writes[0].first==0x4806u && p.loads[0].first==0x4806u);
  assert(p.register_order.size()==2);
  assert(p.register_order[0]==0u && p.register_order[1]==1u);
  uint32_t latest=0;
  for(uint32_t tag:p.register_order) {
    latest=(tag&1u)?p.loads[tag>>1].second:p.writes[tag>>1].second;
  }
  assert(latest==0x00010A2Fu); // GPU must see latest LOAD
  // Check the reversed order: latest SET must prevail over earlier LOAD.
  off=0;
  emit(off,0x2F,{address,0x00010006u,1u});
  emit(off,0x2D,{0x00010006u,0x44A00000u});
  p.Clear();
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),0,off*4,p,0,false,true);
  assert(p.complete && p.register_order.size()==2);
  latest=0;
  for(uint32_t tag:p.register_order) {
    latest=(tag&1u)?p.loads[tag>>1].second:p.writes[tag>>1].second;
  }
  assert(latest==0x44A00000u);
  std::cout<<"actual Hulk NativeX PM4 ordered SET/LOAD scanner: PASS";
}
"""

def main():
    if len(sys.argv)!=2:
        raise SystemExit("Usage: test_patch_native_x_hulk_register_order_cpp_scan.py <ordered NativeX tree>")
    source=(Path(sys.argv[1])/"src/state.cpp").read_text(encoding="utf-8")
    b=source.index("// HULK/PM4-REGISTER-ORDER:")
    e=source.index("// DRAW_INDX (viz query token",b)
    helpers=source[b:e]
    b=source.index("void ScanPackets(")
    e=source.index("\nbool FetchSlotDirty(",b)
    scanner=source[b:e]
    if "HulkOrderedLoad(out" not in scanner or "HULK/PFP-PREDICATE" not in scanner:
        raise ValueError("Expected fully patched Hulk NativeX PM4 scanner")
    prelude=PRELUDE.replace(
        "uint32_t ConstantBase(uint32_t){return 800;}",
        "uint32_t ConstantBase(uint32_t type){return type==1 ? 0x4800 : 0x4000;}")
    if "type==1 ? 0x4800" not in prelude:
        raise ValueError("Unable to model Xenos fetch constant register base")
    prelude=prelude.replace(
        "  std::vector<uint32_t> shader_ucode;",
        "  std::vector<uint32_t> shader_ucode;\n  std::vector<uint32_t> register_order;")
    prelude=prelude.replace(
        "    shader_ucode.clear();draws=indirect=predicated=bin_rejected=bin_rejected_draws=bin_rejected_state=0;complete=true;",
        "    shader_ucode.clear();register_order.clear();draws=indirect=predicated=bin_rejected=bin_rejected_draws=bin_rejected_state=0;complete=true;")
    if "register_order.clear()" not in prelude:
        raise ValueError("Mock PacketScan does not clear ordered state between scans")
    with tempfile.TemporaryDirectory(prefix="hulk_pm4_order_") as tmp:
        root=Path(tmp)
        cpp=root/"test.cpp"
        exe=root/"test.exe"
        cpp.write_text(prelude+helpers+scanner+"\n"+MAIN,encoding="utf-8")
        subprocess.run(["clang++","-std=c++20","-O0","-Wall","-Wextra","-Werror",
                        str(cpp),"-o",str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print("PASS: actual PM4 scanner preserves fetch SET/LOAD chronological order")

if __name__=="__main__":
    main()
