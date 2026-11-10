#!/usr/bin/env python3
"""Compile and run the ACTUAL patched NativeX PM4 scanner, not merely its switch.

Usage: python ci/test_patch_native_x_hulk_bin_cpp_scan.py <NativeX checkout>
Requires clang++ and the Hulk BIN patch already installed on that checkout.
No XEX, game assets, GPU or Vulkan installation is used in the test.
"""
from __future__ import annotations
import subprocess
import sys
import tempfile
from pathlib import Path

PRELUDE = r"""
#include <cstdint>
#include <vector>
#include <utility>
#include <functional>
#include <cstring>
#include <cassert>
#include <iostream>
struct PacketScan {
  std::vector<std::pair<uint32_t,uint32_t>> writes, predicated_writes, loads;
  struct ShaderLoad {uint32_t type,address,dwords,packet; bool immediate; uint32_t ucode_offset;};
  std::vector<ShaderLoad> shader_loads;
  std::vector<uint32_t> shader_ucode;
  static constexpr uint32_t kInGuestMemory=UINT32_MAX;
  uint64_t bin_mask=0xFFFFFFFFull, bin_select=0xFFFFFFFFull;
  uint32_t bin_rejected=0,bin_rejected_draws=0,bin_rejected_state=0,draws=0,indirect=0,predicated=0;
  bool complete=true;
  void Clear() {
    writes.clear();predicated_writes.clear();loads.clear();shader_loads.clear();
    shader_ucode.clear();draws=indirect=predicated=bin_rejected=bin_rejected_draws=bin_rejected_state=0;complete=true;
  }
};
uint32_t Load32(const uint8_t* b,uint32_t addr){
  uint32_t v;std::memcpy(&v,b+addr,4);return v;
}
uint32_t PhysicalToVirtual(uint32_t p){return p;}
const uint8_t* GuestPointer(const uint8_t* b,uint32_t p){return b+p;}
uint32_t ConstantBase(uint32_t){return 800;}
bool SkippedConstants(bool,uint32_t,uint32_t){return false;}
void ScanDraw(const uint8_t*,uint32_t,uint32_t,uint32_t,PacketScan& out){++out.draws;}
"""
MAIN = r"""
int main(){
  std::vector<uint32_t> m(256,0);
  auto emit=[&](int& off,uint32_t op,std::initializer_list<uint32_t> payload,bool pred=false){
    m[off++]=0xC0000000u | ((uint32_t(payload.size())-1)<<16) | (op<<8) | uint32_t(pred);
    for(auto x:payload)m[off++]=x;
  };
  int off=0;
  emit(off,0x50,{0,0});
  emit(off,0x36,{0x1082},true);  // filtered BIN draw
  emit(off,0x2D,{0,123},true);  // filtered BIN constant
  emit(off,0x2D,{0,456},false); // normal constant
  emit(off,0x60,{0x10});       // select low bin bit
  emit(off,0x62,{0x10});
  emit(off,0x2D,{0,789},true); // selected BIN constant
  emit(off,0x36,{0x1082},true); // selected BIN draw
  emit(off,0x3F,{300,3});      // recursive INDIRECT_BUFFER
  m[300/4]=0xC0000000u | (0x36<<8) | 1u;
  m[300/4+1]=0x1082;
  m[300/4+2]=0u;
  PacketScan s;
  uint32_t callback=0;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),0,off*4,s,0,false,false,
              [&]{++callback;});
  assert(s.complete);
  assert(s.draws==2 && callback==2);
  assert(s.bin_rejected==2 && s.bin_rejected_draws==1 && s.bin_rejected_state==1);
  assert(s.writes.size()==2);
  assert(s.writes[0].second==456 && s.writes[1].second==789);
  assert(s.bin_mask==0x10 && s.bin_select==0x10);
  assert(s.indirect==1);
  // A truncated immediate shader load must be rejected before dereferencing
  // the absent second header dword or interpreting memory as microcode.
  const int bad_begin=off;
  emit(off,0x2B,{0});
  PacketScan bad;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),bad_begin*4,off*4,bad,0,false,false);
  assert(!bad.complete);
  const int alu_begin=off;
  emit(off,0x2F,{0,0});
  PacketScan bad_alu;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),alu_begin*4,off*4,bad_alu,0,false,false);
  assert(!bad_alu.complete);
  // A valid IM_LOAD_IMMEDIATE must retain its own immutable microcode
  // even if the guest's command ring is overwritten after scanning.
  const int shader_begin=off;
  emit(off,0x2B,{0,3,0x11111111,0x22222222,0x33333333});
  PacketScan shader;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),shader_begin*4,off*4,
              shader,0,false,false);
  assert(shader.complete && shader.shader_loads.size()==1);
  assert(shader.shader_ucode.size()==3);
  assert(shader.shader_ucode[0]==0x11111111);
  m[shader_begin+3]=0xDEADBEEF;
  assert(shader.shader_ucode[0]==0x11111111);
  // No partial shader is allowed: the parser must not turn the next
  // command packet into the remaining bytes of a truncated program.
  const int truncated_shader_begin=off;
  emit(off,0x2B,{0,5,0x11111111,0x22222222});
  PacketScan truncated_shader;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),truncated_shader_begin*4,off*4,
              truncated_shader,0,false,false);
  assert(!truncated_shader.complete && truncated_shader.shader_loads.empty());
  const int nonzero_start_begin=off;
  emit(off,0x2B,{0,0x00010002,0x11111111,0x22222222});
  PacketScan nonzero_start;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),nonzero_start_begin*4,off*4,
              nonzero_start,0,false,false);
  assert(!nonzero_start.complete && nonzero_start.shader_loads.empty());
  const int invalid_type_begin=off;
  emit(off,0x2B,{2,2,0x11111111,0x22222222});
  PacketScan invalid_type;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),invalid_type_begin*4,off*4,
              invalid_type,0,false,false);
  assert(!invalid_type.complete && invalid_type.shader_loads.empty());
  std::cout<<"full PM4 scanner: passed";
}
"""

def main():
    if len(sys.argv)!=2:
        raise SystemExit("Usage: test_patch_native_x_hulk_bin_cpp_scan.py <patched NativeX checkout>")
    cpp=(Path(sys.argv[1])/"src/state.cpp").read_text(encoding="utf-8")
    begin=cpp.index("void ScanPackets(")
    end=cpp.index("\nbool FetchSlotDirty(",begin)
    source=cpp[begin:end]
    if "HULK/PFP-PREDICATE" not in source or "on_packet_draw" not in source:
        raise ValueError("Expected patched NativeX PM4 scanner not installed")
    with tempfile.TemporaryDirectory() as d:
        src=Path(d)/"hulk_pm4.cpp"
        exe=Path(d)/"hulk_pm4.exe"
        src.write_text(PRELUDE+source+"\n"+MAIN,encoding="utf-8")
        subprocess.run(["clang++","-std=c++20","-O0","-Wall","-Wextra","-Werror",
                        str(src),"-o",str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print("Hulk native PFP full scanner C++ test passed")

if __name__ == "__main__":
    main()
