#!/usr/bin/env python3
"""Execute extracted C++ overlap predicates against an independent byte-set oracle.

Optional SDK_ROOT validates and tests the actual dependency source. No game
assets or GPU are required; cache locking and bucket linkage are not modeled.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from patch_sdk_hulk_index_cache import FILE, OLD, NEW, patch

SDK = Path(sys.argv.pop(1)) if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else None


class IndexCacheTests(unittest.TestCase):
    def test_atomic_idempotent(self):
        self.assertEqual(patch(OLD), (NEW, True))
        self.assertEqual(patch(NEW), (NEW, False))
        for bad in ('drift', OLD + OLD, 'HULK/INDEX-CACHE-OVERLAP'):
            with self.assertRaises(ValueError):
                patch(bad)

    def test_cpu_snapshot_api_and_lifetime(self):
        from patch_sdk_hulk_index_cache import (PROCESS, PROCESS_NEW, READS, DECL,
                                                DECL_NEW, HEADER, patch_snapshot)
        source = ((SDK / FILE).read_text() if SDK else
                  PROCESS + '\n'.join(old for old, n in zip(READS, (1,1,2)) for _ in range(n)))
        header = (SDK / HEADER).read_text() if SDK else DECL
        fixed, decl, _ = patch_snapshot(source, header)
        self.assertEqual(patch_snapshot(fixed,decl), (fixed,decl,False))
        for bad in ('drift', PROCESS_NEW):
            with self.assertRaises(ValueError): patch_snapshot(bad, DECL)
        # Compile the actual production entry points and exercise every exit.
        # The backend conversion and cache are test doubles; full SDK/GPU validation
        # remains required in Windows. All four real CPU reads are transformed above.
        methods = PROCESS_NEW[:PROCESS_NEW.index('bool PrimitiveProcessor::ProcessInternal')]
        code = r'''#include <cstdint>
#include <cstring>
#include <vector>
#include <cassert>
namespace xenos { enum class IndexFormat{kInt16,kInt32}; enum class SourceSelect{kDMA,kAutoIndex}; }
namespace reg { struct VGT_DRAW_INITIATOR {}; struct VGT_DMA_SIZE {}; }
constexpr int XE_GPU_REG_VGT_DMA_BASE=0;
struct State {xenos::IndexFormat index_size=xenos::IndexFormat::kInt16;
  xenos::SourceSelect source_select=xenos::SourceSelect::kDMA; uint32_t num_indices=4,num_words=4;};
struct Registers { State state; uint32_t base=32;
  template<typename T> State Get() const{return state;}
  uint32_t operator[](int) const{return base;}
};
struct SharedMemory {static constexpr uint32_t kBufferSize=0x20000000;};
class PrimitiveProcessor {
 public:
  struct ProcessingResult {uint32_t first=0;};
  Registers register_file_;
  unsigned invalidations=0,live_reads=0; bool fail=false, cached=true;
  bool Process(ProcessingResult&);
  bool ProcessCapturedIndices(ProcessingResult&,const uint8_t*,uint32_t,uint32_t);
  void MemoryInvalidationCallback(uint32_t b,uint32_t n,bool exact) {
    assert(b==32 && n==8 && exact); ++invalidations;cached=false;
  }
  bool ProcessInternal(ProcessingResult& r,const uint8_t* ptr) {
    if(!ptr) {++live_reads;return true;}
    assert(!cached); assert((uintptr_t(ptr)&3)==0);
    r.first=*reinterpret_cast<const uint16_t*>(ptr);cached=true;
    return !fail;
  }
};
''' + methods + r'''
int main(){
  PrimitiveProcessor p; PrimitiveProcessor::ProcessingResult r;
  uint8_t data[9]={0,3,0,4,0,5,0,6,0}; // deliberately unaligned payload
  assert(p.ProcessCapturedIndices(r,data+1,32,8));
  assert(r.first==3 && p.invalidations==2 && !p.cached && p.live_reads==0);
  data[1]=9;
  assert(p.ProcessCapturedIndices(r,data+1,32,8));
  assert(r.first==9 && p.invalidations==4 && !p.cached);
  p.fail=true;
  assert(!p.ProcessCapturedIndices(r,data+1,32,8));
  assert(p.invalidations==6 && !p.cached);
  assert(!p.ProcessCapturedIndices(r,data+1,32,7));
  assert(!p.ProcessCapturedIndices(r,data+1,34,8));
  assert(!p.ProcessCapturedIndices(r,nullptr,32,8));
  p.register_file_.state.num_words=3;
  assert(!p.ProcessCapturedIndices(r,data+1,32,8));
  assert(p.invalidations==6);
  assert(p.Process(r) && p.live_reads==1);
}
'''
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as d:
            cpp=Path(d)/'scope.cpp';exe=Path(d)/'scope.exe';cpp.write_text(code)
            subprocess.run([compiler,'-std=c++17','-O0',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_compiled_overlap_regression(self):
        source = (SDK / FILE).read_text(encoding='utf-8-sig') if SDK else OLD
        fixed, _ = patch(source)
        def condition(body):
            start = body.index('          if (entry_key.base < physical_address_end)')
            return re.findall(r'if \(([^\n]+)\) \{', body[start:])[:2]
        old = condition(OLD)
        new = condition(fixed)
        def function(name, conditions):
            return ('bool '+name+'(uint32_t b,uint32_t entry_end,uint32_t physical_address_start,'
                    'uint32_t physical_address_end) { struct {uint32_t base;} entry_key{b}; return ('+
                    ') && ('.join(conditions)+'); }\n')
        code = '#include <cstdint>\n#include <cassert>\n' + function('before', old) + function('after', new)
        code += r'''
int main() {
  // Exact rewrite, containing write, same end, and either partial overlap.
  assert(!before(100,120,100,120) && after(100,120,100,120));
  assert(!before(100,120,90,130) && after(100,120,90,130));
  assert(!before(100,120,110,120) && after(100,120,110,120));
  assert(after(100,120,90,110) && after(100,120,110,130));
  assert(!after(100,120,80,100) && !after(100,120,120,140));
  // A real read after a same-address rewrite must recompute the conversion.
  int cached_first_index=3, guest_first_index=9;
  if (after(100,120,100,120)) cached_first_index=guest_first_index;
  assert(cached_first_index==9);
  // Independent oracle: enumerate byte addresses, not interval inequalities.
  for (uint32_t b=0;b<16;++b) for(uint32_t e=b+1;e<=16;++e)
    for(uint32_t w=0;w<16;++w) for(uint32_t z=w+1;z<=16;++z) {
      bool shared=false;
      for(uint32_t i=b;i<e;++i) for(uint32_t j=w;j<z;++j) shared |= i==j;
      assert(after(b,e,w,z)==shared);
    }
  assert(after(0x1ffffff0u,0x20000000u,0x1ffffff0u,0x20000000u));
}
'''
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'C++ compiler required')
        with tempfile.TemporaryDirectory(prefix='hulk_index_overlap_') as d:
            cpp=Path(d)/'overlap.cpp'; exe=Path(d)/'overlap.exe'
            cpp.write_text(code,encoding='utf-8')
            subprocess.run([compiler,'-std=c++17','-O0',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    unittest.main()
