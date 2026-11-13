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
namespace rex { namespace graphics {
struct RegisterFile { static constexpr uint32_t kRegisterCount = 0x5000u; };
}}
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
  uint32_t v;std::memcpy(&v,b+(addr & 0x1FFFFFFFu),4);return v;
}
uint32_t PhysicalToVirtual(uint32_t p){return 0xA0000000u | (p & 0x1FFFFFFFu);}
const uint8_t* GuestPointer(const uint8_t* b,uint32_t p){return b+(p & 0x1FFFFFFFu);}
uint32_t ConstantBase(uint32_t t){
  switch(t){case 0:return 0x4000;case 1:return 0x4800;
  case 2:return 0x4900;case 3:return 0x4908;
  case 4:return 0x2000;default:return 0;}
}
bool SkippedConstants(bool,uint32_t,uint32_t){return false;}
void ScanDraw(const uint8_t*,uint32_t,uint32_t,uint32_t,PacketScan& out){++out.draws;}
"""
MAIN = r"""
int main(){
  std::vector<uint32_t> m(4096,0);
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
  // IB_PFD can contain a nested IB, both sharing the same BIN state.
  const int nested_begin=off;
  emit(off,0x37,{300,3});
  m[300/4] = 0xC0013700u;  // nested 0x37, 2 dwords
  m[300/4+1] = 0x380;
  m[300/4+2] = 2;
  m[0x380/4] = 0xC0003601u;  // selected predicated draw
  m[0x380/4+1] = 0x1082;
  PacketScan nested;
  nested.bin_mask=0x10; nested.bin_select=0x10;
  uint32_t nested_callbacks=0;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),nested_begin*4,off*4,
              nested,0,false,false,[&]{++nested_callbacks;});
  assert(nested.complete && nested.indirect==2);
  assert(nested.draws==1 && nested_callbacks==1);
  const int invalid_ib_begin=off;
  emit(off,0x37,{0x300});
  PacketScan bad_ib;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),invalid_ib_begin*4,off*4,
              bad_ib,0,false,false);
  assert(!bad_ib.complete && bad_ib.draws==0);
  // Cyclic guest IB cannot loop indefinitely or spawn unlimited Draws.
  m[300/4] = 0xC0013700u;
  m[300/4+1] = 0x300u;
  m[300/4+2] = 3u;
  PacketScan cyclic;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),nested_begin*4,(nested_begin+3)*4,
              cyclic,0,false,false);
  assert(!cyclic.complete && cyclic.indirect==9 && cyclic.draws==0);
  // #198: false Type0 counts or Type3 reserved bits must not
  // overwrite GPU registers even when their byte length fits a stream.
  const int bad_type0_begin=off;
  m[off++]=0x00004FFFu | (3u << 16);
  m[off++]=1u; m[off++]=2u; m[off++]=3u; m[off++]=4u;
  PacketScan bad_type0;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),
              bad_type0_begin*4,off*4,bad_type0,0,false,false);
  assert(!bad_type0.complete && bad_type0.writes.empty());
  const int good_type0_begin=off;
  m[off++]=0x00004FFEu | (1u << 16);
  m[off++]=0x1122u; m[off++]=0x3344u;
  PacketScan good_type0;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),
              good_type0_begin*4,off*4,good_type0,0,false,false);
  assert(good_type0.complete && good_type0.writes.size()==2);
  const int bad_type3_begin=off;
  m[off++]=0xC0012D04u; m[off++]=0u; m[off++]=42u;
  PacketScan bad_type3;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),
              bad_type3_begin*4,off*4,bad_type3,0,false,false);
  assert(!bad_type3.complete && bad_type3.writes.empty());
  // #198 actual false Type1 header previously consumed 8 float bytes
  // as GPU registers, corrupting later fetch/shader state.
  const int invalid_type1_begin=off;
  m[off++]=0x44800000u; m[off++]=0x3F800000u; m[off++]=0x44340000u;
  PacketScan invalid_type1;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),
              invalid_type1_begin*4,off*4,invalid_type1,0,false,false);
  assert(!invalid_type1.complete && invalid_type1.writes.empty());
  const int valid_type1_begin=off;
  m[off++]=0x40000000u | (0x140u << 11) | 0x120u;
  m[off++]=0xAAAA0001u; m[off++]=0xBBBB0002u;
  PacketScan valid_type1;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),
              valid_type1_begin*4,off*4,valid_type1,0,false,false);
  assert(valid_type1.complete && valid_type1.writes.size()==2);
  assert(valid_type1.writes[0].first==0x120u);
  assert(valid_type1.writes[1].first==0x140u);
  const int invalid_type2_begin=off;
  m[off++]=0xB1800001u;
  PacketScan invalid_type2;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),
              invalid_type2_begin*4,off*4,invalid_type2,0,false,false);
  assert(!invalid_type2.complete);
  const int valid_type2_begin=off;
  m[off++]=0x80000000u;
  PacketScan valid_type2;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),
              valid_type2_begin*4,off*4,valid_type2,0,false,false);
  assert(valid_type2.complete && valid_type2.writes.empty());
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
  // Invalid SET_CONSTANT class must NOT write to register zero.
  const int set_class_begin=off;
  emit(off,0x2D,{0x00070000u,0xDEADBEEFu});
  PacketScan invalid_set;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),set_class_begin*4,off*4,
              invalid_set,0,false,false);
  assert(invalid_set.complete && invalid_set.writes.empty());
  // Fetch slot 31 starts at 0x4fff; a second dword would exceed the GPU file.
  const int set_range_begin=off;
  emit(off,0x2D,{0x000107FFu,0x11111111u,0x22222222u});
  PacketScan invalid_set_range;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),set_range_begin*4,off*4,
              invalid_set_range,0,false,false);
  assert(!invalid_set_range.complete && invalid_set_range.writes.empty());
  // Cross-class writes fit the full GPU register file but corrupt
  // adjacent shader/fetch state, so their PM4 span must fail closed.
  const int fetch_overflow_begin=off;
  emit(off,0x2D,{0x000100BFu,0x11111111u,0x22222222u});
  PacketScan fetch_overflow;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),fetch_overflow_begin*4,
              off*4,fetch_overflow,0,false,false);
  assert(!fetch_overflow.complete && fetch_overflow.writes.empty());
  const int fetch_last_begin=off;
  emit(off,0x2D,{0x000100BFu,0x11111111u});
  PacketScan fetch_last;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),fetch_last_begin*4,
              off*4,fetch_last,0,false,false);
  assert(fetch_last.complete && fetch_last.writes.size()==1 &&
         fetch_last.writes[0].first==0x48BFu);
  const int bool_overflow_begin=off;
  emit(off,0x2D,{0x00020007u,0x11111111u,0x22222222u});
  PacketScan bool_overflow;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),bool_overflow_begin*4,
              off*4,bool_overflow,0,false,false);
  assert(!bool_overflow.complete && bool_overflow.writes.empty());
  const int loop_overflow_begin=off;
  emit(off,0x2D,{0x0003001Fu,0x11111111u,0x22222222u});
  PacketScan loop_overflow;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),loop_overflow_begin*4,
              off*4,loop_overflow,0,false,false);
  assert(!loop_overflow.complete && loop_overflow.writes.empty());
  const int float_overflow_begin=off;
  emit(off,0x2D,{0x000007FFu,0x11111111u,0x22222222u});
  PacketScan float_overflow;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),float_overflow_begin*4,
              off*4,float_overflow,0,false,false);
  assert(!float_overflow.complete && float_overflow.writes.empty());
  const int load_fetch_overflow_begin=off;
  emit(off,0x2F,{0x1000u,0x000100BFu,2u});
  PacketScan load_fetch_overflow;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),
              load_fetch_overflow_begin*4,off*4,load_fetch_overflow,0,false,false);
  assert(!load_fetch_overflow.complete && load_fetch_overflow.loads.empty());
  // SET_CONSTANT2 / SET_SHADER_CONSTANTS beyond GPU register file
  // must fail without submitting corrupted state.
  const int direct_set_begin=off;
  emit(off,0x55,{0x4FFFu,0x12345678u,0x87654321u});
  PacketScan invalid_direct;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),direct_set_begin*4,off*4,
              invalid_direct,0,false,false);
  assert(!invalid_direct.complete && invalid_direct.writes.empty());
  const int shader_const_begin=off;
  emit(off,0x56,{0x5000u,0xAAu});
  PacketScan invalid_shader_const;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),shader_const_begin*4,off*4,
              invalid_shader_const,0,false,false);
  assert(!invalid_shader_const.complete && invalid_shader_const.writes.empty());
  // A syntactically valid LOAD with an invalid register class or range
  // must not read guest memory or append spurious fetch descriptors.
  const int bad_class_begin=off;
  emit(off,0x2F,{0x1000,0x00050000u,1u});
  PacketScan bad_class;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),bad_class_begin*4,off*4,bad_class,0,false,false);
  assert(!bad_class.complete && bad_class.loads.empty());
  const int bad_range_begin=off;
  emit(off,0x2F,{0x1000,0x000107FFu,4095u});
  PacketScan bad_range;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),bad_range_begin*4,off*4,bad_range,0,false,false);
  assert(!bad_range.complete && bad_range.loads.empty());
  // XDK external IM_LOAD: the caller's ScanPackets(copy_microcode=true)
  // owns the actual bytes while they are still current, before Gather.
  // The pinned NativeX address is a 0xA0000000 virtual alias of physical.
  m[0x1400/4]=0x10203040u;
  m[0x1400/4+1]=0x50607080u;
  m[0x1400/4+2]=0x90A0B0C0u;
  const int external_shader_begin=off;
  emit(off,0x27,{0x1401u,3u});
  PacketScan external_shader;
  ScanPackets(reinterpret_cast<uint8_t*>(m.data()),external_shader_begin*4,
              off*4,external_shader,0,false,true);
  assert(external_shader.complete && external_shader.shader_loads.size()==1);
  assert(external_shader.shader_loads[0].type==1u);
  assert(external_shader.shader_loads[0].ucode_offset==0u);
  assert(external_shader.shader_ucode.size()==3u);
  assert(external_shader.shader_ucode[0]==0x10203040u);
  m[0x1400/4]=0xDEADBEEFu;  // guest recycled shader allocation
  assert(external_shader.shader_ucode[0]==0x10203040u); // immutable copy
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
