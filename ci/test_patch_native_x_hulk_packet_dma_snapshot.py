#!/usr/bin/env python3
"""Regression tests: snapshot only packet-time DMA indices for Hulk NativeX."""
import unittest
import subprocess
import shutil
import tempfile
from pathlib import Path
from patch_native_x_hulk_packet_dma_snapshot import (
    ANCHOR, ADDITION, CALL_ANCHOR, CALL_NEW, patch_text,
    RESTORE, AHEAD, RESTORE_AFTER, patch_draw,
    INLINE_OLD, INLINE_NEW, INLINE_CALL_OLD, INLINE_CALL_NEW,
    patch_inline_capture, patch_renderer_all
)

class DmaSnapshotTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('clang++') or shutil.which('g++'), 'C++ compiler missing')
    def test_gpu_restore_uses_captured_bytes_not_live_cpu_equality(self):
        from patch_native_x_hulk_packet_dma_snapshot import MEMORY_OLD, MEMORY_NEW, patch_memory
        self.assertEqual(patch_memory(MEMORY_OLD), (MEMORY_NEW, True))
        self.assertEqual(patch_memory(MEMORY_NEW), (MEMORY_NEW, False))
        with self.assertRaises(ValueError):
            patch_memory(MEMORY_OLD.replace('std::memcmp', 'changed'))
        harness = r'''
#include <array>
#include <cstdint>
#include <cstring>
#include <cassert>
constexpr int RESOURCE_STATE_TRANSITION_MODE_TRANSITION=0;
struct IDeviceContext {
  std::array<uint8_t,64> gpu{}; unsigned updates=0;
  void UpdateBuffer(int, uint32_t a, uint32_t n, const uint8_t* p, int) {
    ++updates; std::memcpy(gpu.data()+a,p,n);
  }
};
struct GuestMemory {
  static constexpr uint32_t kBufferSize=64;
  std::array<uint8_t,64> live{};
  std::array<uint64_t,16> page_hashes_{};
  std::array<bool,16> copy_valid_{};
  std::array<unsigned,16> checked_window_{};
  uint64_t uploaded_bytes_=0, copies_=0, total_copies_=0;
  bool in_read_state_=true; int buffer_=0; unsigned invalidations=0;
  GuestMemory& memory() { return *this; }
  uint8_t* TranslatePhysical(uint32_t a) { return live.data()+a; }
  unsigned page_size_log2() { return 2; }
  void MemoryInvalidationCallback(uint32_t, uint32_t, bool) { ++invalidations; }
  bool WriteIfChanged(IDeviceContext*, uint32_t, const uint8_t*, uint32_t);
};
'''
        scenario = r'''
int main() {
  GuestMemory m; IDeviceContext ctx;
  // Two overlapping copies from one command. The final copy equals LIVE
  // memory, but the preceding copy has just changed the GPU mirror.
  uint8_t a[]={1,2,3,4,5,6,7,8}, b[]={9,10,11,12};
  std::memcpy(m.live.data()+18,b,4);
  m.WriteIfChanged(&ctx,16,a,8);
  m.WriteIfChanged(&ctx,18,b,4);
  const uint8_t expected[]={1,2,9,10,11,12,7,8};
  if (std::memcmp(ctx.gpu.data()+16,expected,8)) return 42;
  // No preceding copy is needed to trigger the second failure case: CPU
  // memory already matches capture while the independently modified GPU
  // range still contains different bytes.
  ctx.gpu[18]=99;
  m.WriteIfChanged(&ctx,18,b,4);
  assert(ctx.gpu[18]==9);
  assert(ctx.updates==3 && m.invalidations==3);
  assert(!m.in_read_state_ && m.uploaded_bytes_==16);
  assert(std::memcmp(m.live.data()+18,b,4)==0); // no guest-memory mutation
  assert(!m.WriteIfChanged(&ctx,64,b,1));
  assert(!m.WriteIfChanged(&ctx,63,b,2));
  assert(!m.WriteIfChanged(&ctx,18,b,0));
  assert(!m.WriteIfChanged(&ctx,18,nullptr,4));
  assert(ctx.updates==3);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            compiler=shutil.which('clang++') or shutil.which('g++')
            for label, body, expected in [('before', MEMORY_OLD, 42), ('after', MEMORY_NEW, 0)]:
                cpp=root/(label+'.cpp'); exe=root/label
                cpp.write_text(harness+body+scenario)
                subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
                self.assertEqual(subprocess.run([str(exe)]).returncode, expected)

    def test_cpu_snapshot_copy_order_and_holes(self):
        from patch_native_x_hulk_packet_dma_snapshot import CPU_HELPER, CPU_ANCHOR, patch_cpu_draw
        original = 'struct DrawContext {\n' + CPU_ANCHOR
        patched, changed = patch_cpu_draw(original)
        self.assertTrue(changed)
        self.assertEqual(patch_cpu_draw(patched), (patched, False))
        with self.assertRaises(ValueError):
            patch_cpu_draw('struct DrawContext {')
        code = r'''#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cassert>
struct Copy { uint32_t address; const uint8_t* data; uint32_t size; };
struct Processor {
  unsigned live_reads=0, snapshot_reads=0;
  std::vector<uint8_t> consumed;
  bool Process(int&) { ++live_reads; return true; }
  bool ProcessCapturedIndices(int&, const uint8_t* p, uint32_t, uint32_t n) {
    ++snapshot_reads; consumed.assign(p,p+n); return true;
  }
};
''' + CPU_HELPER + r'''
int main() {
  Processor p; int result=0;
  uint8_t a[]={1,2,3,4,5,6,7,8}, b[]={9,10,11,12};
  std::vector<Copy> copies{{32,a,8},{34,b,4}};
  assert(HulkProcessIndexCopies(p,result,32,8,copies));
  assert((p.consumed==std::vector<uint8_t>{1,2,9,10,11,12,7,8}));
  assert(p.live_reads==0 && p.snapshot_reads==1);
  // Mutating live/command storage after processing cannot change consumed bytes.
  a[0]=99; assert(p.consumed[0]==1);
  // Two partial ranges may jointly cover the indices; later writes win.
  copies={{32,a,4},{36,b,4}};
  assert(HulkProcessIndexCopies(p,result,32,8,copies));
  assert((p.consumed==std::vector<uint8_t>{99,2,3,4,9,10,11,12}));
  copies={{32,a,3},{36,b,4}};
  assert(!HulkProcessIndexCopies(p,result,32,8,copies));
  assert(p.live_reads==0 && p.snapshot_reads==2); // hole must not use live memory
  copies={{32,nullptr,8}};
  assert(!HulkProcessIndexCopies(p,result,32,8,copies));
  copies={{16,a,8}};
  // No DMA capture -> fail closed, never read possibly reused guest bytes.
  assert(!HulkProcessIndexCopies(p,result,32,8,copies) && p.live_reads==0);
  assert(!HulkProcessIndexCopies(p,result,0x1ffffffe,4,copies));
  assert(!HulkProcessIndexCopies(p,result,0,0xffffffff,copies));
  assert(!HulkProcessIndexCopies(p,result,0,0,copies));
  // Exhaustive small-range coverage and last-writer precedence oracle.
  for(unsigned x=0;x<8;++x) for(unsigned n=1;n<=8-x;++n)
    for(unsigned y=0;y<8;++y) for(unsigned m=1;m<=8-y;++m) {
      copies={{32+x,a,n},{32+y,a,m}};
      std::vector<uint8_t> expected(8,0); std::vector<bool> seen(8,false);
      for(unsigned i=0;i<n;++i){expected[x+i]=a[i];seen[x+i]=true;}
      for(unsigned i=0;i<m;++i){expected[y+i]=a[i];seen[y+i]=true;}
      const bool full=std::all_of(seen.begin(),seen.end(),[](bool v){return v;});
      const unsigned old_live=p.live_reads;
      assert(HulkProcessIndexCopies(p,result,32,8,copies)==full);
      assert(p.live_reads==old_live);
      if(full) assert(p.consumed==expected);
    }
}
'''
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as d:
            cpp=Path(d)/'snapshot.cpp'; exe=Path(d)/'snapshot.exe'
            cpp.write_text(code)
            subprocess.run([compiler,'-std=c++17','-O0',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_actual_cpp_snapshot_and_upload_order(self):
        # Compile the production helper. Only map guest physical memory to a
        # small host array instead of reserving the 4 GiB guest address arena.
        helper = ADDITION.replace('base + (0xA0000000u | capture_base)', 'base + capture_base')
        before = RESTORE + '  if (primitives_->UploadConverted()) ++stats_.copies;\n' + AHEAD
        after, changed = patch_draw(before)
        self.assertTrue(changed)
        self.assertEqual(patch_draw(after), (after, False))
        with self.assertRaises(ValueError):
            patch_draw('unknown layout')
        prelude = r'''
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cassert>
#include <functional>
#define REXLOG_WARN(...) ((void)0)
#define REXLOG_INFO(...) ((void)0)
namespace rex::graphics {
enum { XE_GPU_REG_VGT_DRAW_INITIATOR, XE_GPU_REG_VGT_DMA_BASE, XE_GPU_REG_VGT_DMA_SIZE };
}
struct StateTracker {
  std::array<uint32_t,3> regs{};
  bool current_write=true;
  bool LastWrite(uint32_t r,uint32_t& v) const {
    if(!current_write) return false;
    v=regs[r];return true;
  }
  uint32_t HulkRegisterAtDraw(uint32_t r) const { return regs[r]; }
};
struct Command {
  struct Copy {uint32_t address,offset,size;};
  std::array<Copy,2> copies{};
  uint32_t copy_count=0;
  std::vector<uint8_t> data;
};
struct Memory {
  uint8_t mirror=0, live=9;
  bool invalid=false;
  uint64_t copies() const {return 1;}
  bool WriteIfChanged(int,uint32_t,const uint8_t* p,uint32_t) {
    if (*p==live) return false;
    mirror=*p;invalid=true;return true;
  }
  void Ahead() {if(invalid) {mirror=live;invalid=false;}}
};
struct Primitive {bool UploadConverted(){return false;}};
struct Copy {uint32_t address;const uint8_t* data;uint32_t size;};
struct Stats {uint32_t copies=0,inline_restored=0;};
'''
        def render_function(name, body):
            return ('uint8_t '+name+'() {\n'
                    'Memory memory; auto* memory_=&memory; Primitive primitive; auto* primitives_=&primitive;\n'
                    'Stats stats_; int context=0; uint64_t copies_at_start=0; uint8_t saved=3;\n'
                    'std::array<Copy,1> copies{{{0,&saved,1}}};\n'
                    'std::function<void()> upload_ahead_=[&]{memory.Ahead();};\n'
                    +body+'return memory.mirror;\n}\n')
        main = r'''
int main() {
  std::array<uint8_t,128> guest{};
  guest[32]=3;guest[33]=4;guest[34]=5;guest[35]=6;
  StateTracker tracker{{(2u<<16)|4u,33u,2u}};
  Command a;
  assert(CapturePacketDmaIndices(tracker,guest.data(),a,false));
  assert(a.copy_count==1 && a.copies[0].address==32 && a.data.size()==4);
  guest[32]=9;guest[33]=8;
  assert(a.data[0]==3 && a.data[1]==4);
  Command b;
  assert(CapturePacketDmaIndices(tracker,guest.data(),b,false));
  assert(b.data[0]==9 && b.data[1]==8 && a.data[0]==3);
  tracker.regs[2]=1;
  Command short_dma;
  assert(!CapturePacketDmaIndices(tracker,guest.data(),short_dma,false));
  assert(short_dma.copy_count==0 && short_dma.data.empty());
  tracker.regs[2]=2;tracker.regs[1]=0x1ffffffe;
  Command outside;
  assert(!CapturePacketDmaIndices(tracker,guest.data(),outside,false));
  assert(outside.data.empty());
  // Build 177: the authentic XDK draw may inherit its initiator from
  // the prior PM4 state without writing a new one in this call.
  tracker.regs[1]=33;
  tracker.current_write=false;
  Command genuine;
  assert(CapturePacketDmaIndices(tracker,guest.data(),genuine,false));
  assert(genuine.copy_count==1 && genuine.data.size()==4);
  Command synthetic;
  assert(!CapturePacketDmaIndices(tracker,guest.data(),synthetic,true));
  assert(synthetic.copy_count==0 && synthetic.data.empty());
  // Xenos DMA_SIZE.swap_mode bits 30-31, 16-bit index mode 3=k16in32:
  // reverse the two 16-bit halves of EACH 32-bit physical word. This was
  // silently disabled in ReXGlue PrimitiveProcessor, creating bad triangles.
  tracker.current_write=true;
  tracker.regs[1]=32;
  tracker.regs[2]=(3u<<30)|2u;
  guest[32]=1;guest[33]=2;guest[34]=3;guest[35]=4;
  Command mode3;
  assert(CapturePacketDmaIndices(tracker,guest.data(),mode3,false));
  assert(mode3.copy_count==1 && mode3.copies[0].address==32);
  assert((mode3.data==std::vector<uint8_t>{3,4,1,2}));
  // Mode 2 (k8in32) also exchanges halfwords; PrimitiveProcessor then
  // performs the separate 8-in-16 byte swap, yielding full 32-bit reversal.
  tracker.regs[2]=(2u<<30)|2u;
  Command mode2;
  assert(CapturePacketDmaIndices(tracker,guest.data(),mode2,false));
  assert((mode2.data==std::vector<uint8_t>{3,4,1,2}));
  // Index base in the HIGH half of a word, odd count: copy the enclosing
  // word without leaking into the neighbouring (unsnapshotted) command.
  tracker.regs[0]=(1u<<16)|4u;
  tracker.regs[1]=34;
  tracker.regs[2]=(3u<<30)|1u;
  Command high_half;
  assert(CapturePacketDmaIndices(tracker,guest.data(),high_half,false));
  assert(high_half.copies[0].address==32 && high_half.copies[0].size==4);
  assert((high_half.data==std::vector<uint8_t>{3,4,1,2}));
  tracker.regs[0]=(2u<<16)|4u; tracker.regs[2]=2u;
  assert(old_order()==9);  // reproduces overwritten snapshot
  assert(new_order()==3);  // current draw retains its captured bytes
}
'''
        with tempfile.TemporaryDirectory(prefix='hulk_dma_cpp_') as tmp:
            cpp=Path(tmp)/'dma.cpp'; exe=Path(tmp)/'dma.exe'
            cpp.write_text(prelude+helper+render_function('old_order',before)+
                           render_function('new_order',after)+main,encoding='utf-8')
            compiler = shutil.which('clang++') or shutil.which('g++')
            if not compiler: self.fail('C++ compiler required for DMA lifetime regression')
            subprocess.run([compiler,'-std=c++20','-O0',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    @unittest.skipUnless(shutil.which('clang++') or shutil.which('g++'),
                         'C++ compiler missing')
    def test_inline_snapshot_bounds_and_ownership_cpp(self):
        # Compile the exact production C++ implementation, replacing only
        # the virtual 0xA0000000 guest mapping with a small test-memory array.
        real = INLINE_NEW.replace(
            'base + (0xA0000000u | address)', 'base + address')
        self.assertIn('HULK/INLINE-SNAPSHOT-BOUNDS', real)
        code = r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>
#include <utility>
namespace rex { namespace graphics {
enum : uint32_t {
  XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0=0,
  XE_GPU_REG_VGT_DRAW_INITIATOR=200,
  XE_GPU_REG_VGT_DMA_BASE=201,
};
namespace xenos {
enum class FetchConstantType : uint32_t {kVertex=3};
}
}}
struct StateTracker {
  std::array<uint32_t,256> reg{};
  std::array<bool,256> present{};
  bool LastWrite(uint32_t r, uint32_t& out) const {
    if (!present.at(r)) return false;
    out=reg[r]; return true;
  }
};
struct Command {
  struct Copy {uint32_t address,offset,size;};
  std::array<Copy,2> copies{};
  uint32_t copy_count=0;
  std::vector<uint8_t> data;
};
""" + real + r"""
int main() {
  std::array<uint8_t,512> guest{};
  for (int i=0;i<512;++i) guest[i]=uint8_t(i);
  StateTracker tracker{};
  constexpr uint32_t v0=31u*6u+4u, v1=v0+1u;
  using rg=rex::graphics::RegisterFile; // intentionally not needed
  tracker.present[v0]=tracker.present[v1]=true;
  tracker.reg[v0]=64u|3u; // valid 16-byte inline vertex payload
  tracker.reg[v1]=4u<<2;
  tracker.present[200]=tracker.present[201]=true;
  tracker.reg[200]=(2u<<16)|4u; // two 16-bit DMA indices
  tracker.reg[201]=96u;
  Command c;
  assert(CaptureInlineData(tracker,guest.data(),c));
  assert(c.copy_count==2 && c.copies[0].address==64 &&
         c.copies[0].size==16 && c.copies[1].address==96 &&
         c.copies[1].size==4 && c.data.size()==20);
  const uint8_t first=c.data[0], last=c.data[16];
  guest[64]=0xff;guest[96]=0xff;
  assert(c.data[0]==first && c.data[16]==last);
  // Previous implementation masked a bogus high GPU pointer into an
  // unrelated low page and published its incorrect vertices.
  tracker.reg[v0]=0xA0000043u;
  Command high_alias;
  assert(!CaptureInlineData(tracker,guest.data(),high_alias));
  assert(high_alias.copy_count==0 && high_alias.data.empty());
  // A physical base that crosses 512 MiB must be rejected without
  // touching a single byte of mapped guest memory.
  tracker.reg[v0]=0x1FFFFFF7u;
  Command overflow;
  assert(!CaptureInlineData(tracker,guest.data(),overflow));
  assert(overflow.copy_count==0);
  tracker.reg[v0]=64u|3u;
  tracker.reg[v1]=0u;
  Command empty;
  assert(!CaptureInlineData(tracker,guest.data(),empty));
  tracker.reg[v1]=4u<<2;
  tracker.reg[201]=0x1FFFFFFEu;
  Command bad_dma;
  assert(!CaptureInlineData(tracker,guest.data(),bad_dma));
  assert(bad_dma.copy_count==1); // safely copied first range, not published
  // A missing inline fetch slot is NOT automatically an error: prior
  // NativeX paths may use persistent vertex bindings.
  tracker.present.fill(false);
  Command inherited;
  assert(CaptureInlineData(tracker,guest.data(),inherited));
  assert(inherited.copy_count==0);
}
"""
        code = code.replace(
            '  using rg=rex::graphics::RegisterFile; // intentionally not needed\n',
            '')
        with tempfile.TemporaryDirectory(prefix="hulk_inline_snapshot_") as d:
            src=Path(d)/"inline.cpp"; exe=Path(d)/"inline"
            src.write_text(code,encoding="utf8")
            compiler=shutil.which("clang++") or shutil.which("g++")
            subprocess.run([compiler,"-std=c++20","-O0","-Wall",
                            "-Wextra","-Werror",str(src),"-o",str(exe)],
                           check=True,capture_output=True,text=True)
            subprocess.run([str(exe)],check=True,capture_output=True,text=True)

    def test_inline_snapshot_atomic_and_draw_cancel(self):
        source = INLINE_OLD + "\n" + INLINE_CALL_OLD
        patched, changed = patch_inline_capture(source)
        self.assertTrue(changed)
        self.assertEqual(patch_inline_capture(patched),(patched,False))
        self.assertIn("if (!CaptureInlineData(tracker, base, c))",patched)
        self.assertIn("c.type = Command::Type::kNone",patched)
        self.assertNotIn("address & 0x1FFFFFFFu",patched)
        with self.assertRaises(ValueError):
            patch_inline_capture(source.replace(INLINE_OLD,"unreviewed inline copy"))
        full = ANCHOR + "\n" + CALL_ANCHOR + "\n" + INLINE_OLD + "\n" + INLINE_CALL_OLD
        updated, change = patch_renderer_all(full)
        self.assertTrue(change)
        self.assertEqual(patch_renderer_all(updated),(updated,False))
        self.assertIn("HULK/PM4-DMA-SNAPSHOT",updated)
        self.assertIn("HULK/INLINE-SNAPSHOT-BOUNDS",updated)

    def test_atomic_idempotent(self):
        original = ANCHOR + "\n" + CALL_ANCHOR
        updated,changed=patch_text(original)
        self.assertTrue(changed)
        self.assertEqual(patch_text(updated),(updated,False))
        with self.assertRaises(ValueError):patch_text(ADDITION+CALL_ANCHOR)
        with self.assertRaises(ValueError):patch_text("unrecognized revision")

    def test_no_fabricated_index_data(self):
        for token in (
            "tracker.LastWrite(rg::XE_GPU_REG_VGT_DRAW_INITIATOR",
            "HULK/XDK-PERSISTENT-INITIATOR",
            "initiator = tracker.HulkRegisterAtDraw(rg::XE_GPU_REG_VGT_DRAW_INITIATOR)",
            "tracker.HulkRegisterAtDraw(rg::XE_GPU_REG_VGT_DMA_BASE",
            "tracker.HulkRegisterAtDraw(rg::XE_GPU_REG_VGT_DMA_SIZE",
            "if (!requested_indices || requested_indices > available_indices)",
            "return reject(\"DMA index count missing/short\")",
            "return reject(\"DMA address/length invalid\")",
            "capture_bytes > kMaxSnapshotBytes",
            "uint64_t(capture_base) + capture_bytes > kGuestPhysicalBytes",
            "c.copy_count >= c.copies.size()",
            "c.data.insert(c.data.end(), source, source + capture_bytes)",
            "HULK/PM4-DMA-INDEX-ENDIAN",
            "const bool pair_swap = stride == 2u && swap_mode >= 2u",
            "std::swap(c.data[offset + i], c.data[offset + i + 2])",
            "HULK/PM4-DMA-ADDRESS-PARITY",
            "const uint32_t physical = dma_base & ~(stride - 1u)",
            "c.copies[c.copy_count++] = {capture_base, offset, uint32_t(capture_bytes)}",
            "[HULK/PM4-DMA-SNAPSHOT]"
        ):
            self.assertIn(token, ADDITION)
        self.assertIn("tracked.call == Call::kDrawIndexedVertices", CALL_NEW)
        self.assertIn("!CapturePacketDmaIndices(tracker, base, c, packet_surfaces != nullptr)", CALL_NEW)
        self.assertIn("c.type = Command::Type::kNone;", CALL_NEW)
        self.assertNotIn("base, c)) return;", CALL_NEW)
        self.assertIn("bool CapturePacketDmaIndices(", ADDITION)
        self.assertIn("const uint32_t indices = requested_indices;", ADDITION)
        self.assertIn("bool packet_draw)", ADDITION)
        self.assertIn("[HULK/PM4-DMA-REJECT]", ADDITION)
        self.assertIn("return true;  // Xenos kAutoIndex", ADDITION)
        self.assertIn("HULK/PM4-PRIMITIVE-SOURCE", ADDITION)
        self.assertIn('return reject("invalid Xenos primitive type")', ADDITION)
        self.assertIn('return reject("PM4 immediate/reserved indices not captured")', ADDITION)
        self.assertNotIn("std::min(requested_indices, available_indices)", ADDITION)
        self.assertNotIn("memset",ADDITION)


    def test_rejected_dma_draw_must_not_publish_as_draw(self):
        # NativeX upstream Capture::~Capture publishes the command even on
        # early return. Its Execute path applies the register record first,
        # then ignores Command::Type::kNone.
        self.assertIn("HULK/PM4-DMA-CANCEL", CALL_NEW)
        self.assertIn("Capture::~Capture()", CALL_NEW)
        self.assertIn("c.type = Command::Type::kNone;", CALL_NEW)
        self.assertIn("return;", CALL_NEW)
        def execute(command_type, record, old_state):
            state = record if record is not None else old_state
            rendered = command_type == "kDraw"
            return state, rendered
        self.assertEqual(execute("kNone", 7, 0), (7, False))
        self.assertEqual(execute("kDraw", 7, 0), (7, True))

    def test_xenos_primitive_and_index_source_classification(self):
        # Cross-checked against Xenia xenos::PrimitiveType and SourceSelect:
        # 0: no primitive, 1..8 and 12..22 are defined primitive values.
        # Source 0=DMA, 1=immediate, 2=auto, 3=reserved.
        # Immediate source requires copying packet-embedded indices, which
        # this path does NOT do; passing it through uses stale GPU DMA state.
        def decision(initiator):
            primitive = initiator & 0x3F
            source_select = (initiator >> 6) & 3
            if not (1 <= primitive <= 8 or 12 <= primitive <= 22):
                return "reject_primitive"
            if source_select == 2:
                return "auto_index"
            if source_select != 0:
                return "reject_uncaptured_source"
            return "dma_snapshot"
        self.assertEqual(decision((180 << 16) | 4), "dma_snapshot")
        self.assertEqual(decision((180 << 16) | (2 << 6) | 4), "auto_index")
        self.assertEqual(decision((180 << 16) | (1 << 6) | 4),
                         "reject_uncaptured_source")
        self.assertEqual(decision((180 << 16) | (3 << 6) | 4),
                         "reject_uncaptured_source")
        self.assertEqual(decision((180 << 16) | 49), "reject_primitive")
        for primitive in (0, 9, 10, 11, 23, 31, 49, 63):
            self.assertEqual(decision(primitive), "reject_primitive")
        for primitive in (1, 2, 3, 4, 5, 6, 7, 8, 12, 13,
                          14, 15, 16, 17, 18, 19, 20, 21, 22):
            self.assertEqual(decision(primitive), "dma_snapshot")

    def test_exact_sdk_dma_index_address_parity(self):
        # Verified from bundled ReXGlue SDK
        # src/graphics/primitive_processor.cpp:
        # guest_index_base = regs[VGT_DMA_BASE] & ~(stride - 1)
        # SharedMemory::kBufferSize = 1 << 29.
        self.assertIn("HULK/PM4-DMA-ADDRESS-PARITY", ADDITION)
        self.assertIn("const uint32_t physical = dma_base & ~(stride - 1u)", ADDITION)
        self.assertIn("{capture_base, offset, uint32_t(capture_bytes)}", ADDITION)
        self.assertNotIn("const uint32_t physical = dma_base & 0x1FFFFFFFu", ADDITION)
        self.assertNotIn("{dma_base, uint32_t(c.data.size()), uint32_t(bytes)}", ADDITION)
        self.assertIn("const uint32_t capture_base = pair_swap ? (physical & ~3u) : physical;", ADDITION)

        def snapshot_span(address, requested, stride):
            physical = address & ~(stride - 1)
            size = requested * stride
            if not size or size > (1 << 20) or physical + size > (1 << 29):
                return None
            return (physical, size)

        self.assertEqual(snapshot_span(0x1721D701, 24, 2),
                         (0x1721D700, 48))
        self.assertEqual(snapshot_span(0x1721D703, 24, 4),
                         (0x1721D700, 96))
        self.assertEqual(snapshot_span(0x1721D700, 24, 2),
                         (0x1721D700, 48))
        # Masking to 29 bits used to turn this INVALID host address into
        # a valid low-memory address and capture the wrong index bytes.
        self.assertIsNone(snapshot_span(0xA0010001, 24, 2))
        self.assertIsNone(snapshot_span(0x20000000, 24, 2))
        self.assertIsNone(snapshot_span(0x1FFFFFFF, 24, 4))
        self.assertIsNone(snapshot_span(0x1721D701, 0, 2))
        self.assertIsNone(snapshot_span(0x1721D700, 524289, 2))

    def test_persistent_dma_register_values(self):
        # Packet-local LastWrite cannot observe the last valid DMA BASE/SIZE.
        # Synthetic draws must carry their own initiator. Genuine XDK
        # DrawIndexedVertices may inherit an effective GPU initiator.
        self.assertIn("HULK/PM4-PERSISTENT-DMA", ADDITION)
        self.assertIn("tracker.LastWrite(rg::XE_GPU_REG_VGT_DRAW_INITIATOR", ADDITION)
        self.assertIn("if (packet_draw)", ADDITION)
        self.assertIn('return reject("synthetic packet lacks draw initiator")', ADDITION)
        self.assertIn("initiator = tracker.HulkRegisterAtDraw(", ADDITION)
        self.assertIn("dma_base = tracker.HulkRegisterAtDraw(", ADDITION)
        self.assertIn("dma_size = tracker.HulkRegisterAtDraw(", ADDITION)
        self.assertNotIn("tracker.LastWrite(rg::XE_GPU_REG_VGT_DMA_BASE", ADDITION)
        self.assertNotIn("tracker.LastWrite(rg::XE_GPU_REG_VGT_DMA_SIZE", ADDITION)
        # Legal inherited binding: earlier packet wrote base/size, current
        # packet updates only initiator; request must stay within limits.
        def accepted(initiator, effective_base, effective_size):
            primitive = initiator & 0x3F
            if not (1 <= primitive <= 8 or 12 <= primitive <= 22):
                return False
            source = (initiator >> 6) & 3
            if source == 2:
                return True
            if source != 0:
                return False
            requested = initiator >> 16
            available = effective_size & 0xFFFFFF
            stride = 4 if (initiator >> 11) & 1 else 2
            physical = effective_base & ~(stride - 1)
            return (bool(requested) and requested <= available and
                    requested * stride <= (1 << 20) and
                    physical + requested * stride <= 0x20000000)
        self.assertTrue(accepted((24 << 16) | 4, 0x1721D700, 24))
        self.assertFalse(accepted((25 << 16) | 4, 0x1721D700, 24))
        self.assertFalse(accepted((24 << 16) | 4, 0x1721D700, 0))
        self.assertFalse(accepted((0 << 16) | 4, 0x1721D700, 24))
        self.assertFalse(accepted((24 << 16) | 4, 0x1FFFFFF0, 24))

if __name__=="__main__":unittest.main()
