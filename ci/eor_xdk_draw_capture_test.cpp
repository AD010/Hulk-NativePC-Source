#include "../native_port/src/eor_xdk_draw_capture.h"
#include <cassert>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
#include <cstdio>
#include <cstdlib>

#undef assert
#define assert(check) do { if (!(check)) { \
  std::fprintf(stderr,"[HULK/TEST] %s:%d: %s\n",__FILE__,__LINE__,#check); \
  std::fflush(stderr); std::abort(); } } while(false)
using namespace hulk::eor::xdk;
namespace {
struct GuestMemory {
  std::vector<std::byte> bytes=std::vector<std::byte>(0x6000);
};
bool ReadFake(void* state,uint32_t address,std::byte* output,size_t size) noexcept {
  const auto& data=static_cast<GuestMemory*>(state)->bytes;
  if(!output || address>data.size() || size>data.size()-address)return false;
  std::memcpy(output,data.data()+address,size);
  return true;
}
void W(GuestMemory& m,uint32_t address,uint32_t v) {
  auto* p=m.bytes.data()+address;
  for(int i=0;i<4;++i)p[i]=std::byte(uint8_t(v>>(24-8*i)));
}
void CheckIndexDmaFormats() {
  GuestMemory m;
  constexpr uint32_t object=0x200;
  const std::array<std::byte,8> bytes{std::byte{0},std::byte{1},std::byte{2},std::byte{3},
                                     std::byte{4},std::byte{5},std::byte{6},std::byte{7}};
  constexpr uint32_t expected16[]={0x0100,0x0001,0x0203,0x0302};
  constexpr uint32_t expected32[]={0x03020100,0x02030001,0x00010203,0x01000302};
  W(m,object+24,0x4000); W(m,object+28,8);
  for(uint32_t width:{2u,4u})for(uint32_t mode=0;mode<4;++mode) {
    W(m,object,((width==4u)?0x80000000u:0u)|(mode<<29)|2u);
    auto binding=ReadUsaIndexBufferBinding(object,ReadFake,&m);
    assert(binding && binding->element_bytes==width && binding->endian==mode);
    assert(binding->Read(bytes,0)==(width==2u?expected16[mode]:expected32[mode]));
    assert(!binding->Read(bytes,8/width));
  }
  W(m,object,(2u<<29)|2u);
  W(m,object+24,0x4002);W(m,object+28,6);
  auto interior=ReadUsaIndexBufferBinding(object,ReadFake,&m);
  const auto six=std::span(bytes).first(6);
  assert(interior && !interior->Read(six,0) && interior->Read(six,1)==0x0405u);
  W(m,object+24,0x4001);assert(!ReadUsaIndexBufferBinding(object,ReadFake,&m));
  W(m,object+24,0xfffffffc);W(m,object+28,8);assert(!ReadUsaIndexBufferBinding(object,ReadFake,&m));
  W(m,object+24,0x4000);W(m,object+28,7);assert(!ReadUsaIndexBufferBinding(object,ReadFake,&m));
}
void CheckBoundVertexWindow() {
  GuestMemory m;
  constexpr uint32_t device=0x100,object=0x4000,slot=7;
  const uint32_t pair=device+(239-slot)*8;
  // Nonzero fetch type/endian/high bits are part of the packed XDK fields.
  W(m,object+24,0xf1234003);W(m,object+28,0x10000182);
  W(m,pair,0x11235023);W(m,pair+4,0x10000162);
  m.bytes[device+12528+slot]=std::byte{7};
  const auto bound=[&]{return ReadUsaVertexStreamBinding(device,slot,object,ReadFake,&m);};
  auto b=bound();
  assert(b && b->object==object && b->allocation_address==0xf1234000 &&
         b->allocation_bytes==384 && b->offset==32 && b->length==352 && b->stride==28);
  W(m,pair,0x11235003);assert(!bound());
  W(m,pair,0x11235023);W(m,pair+4,0x10000160);assert(!bound());
  W(m,pair+4,0x100001a2);assert(!bound());
  W(m,pair+4,0x10000162);W(m,object+24,0xf1234002);assert(!bound());
  assert(!ReadUsaVertexStreamBinding(UINT32_MAX,slot,object,ReadFake,&m));
  assert(!ReadUsaVertexStreamBinding(device,16,object,ReadFake,&m));
}
void CheckBufferSnapshots() {
  GuestMemory guest;
  const uint32_t object=0x100, address=0x200, bytes=12;
  W(guest,address,0x12345678);
  OriginalGuestBufferSnapshots buffers(2*bytes);
  const auto first=buffers.Capture(object,address,bytes,ReadFake,&guest);
  assert(first && first->guest_size==bytes && buffers.RetainedBytes()==bytes);
  // Thousands of Draws sharing a static VB must not consume the frame
  // budget thousands of times (the former capture exhausted it on Draw 3).
  for(unsigned draw=0;draw<4096;++draw)
    assert(buffers.Capture(object,address,bytes,ReadFake,&guest)==first);
  assert(buffers.RetainedBytes()==bytes);
  W(guest,address,0xABCDEF01);
  const auto rewritten=buffers.Capture(object,address,bytes,ReadFake,&guest);
  assert(rewritten && rewritten!=first && rewritten->bytes!=first->bytes);
  assert(first->bytes[0]==std::byte{0x12});
  assert(rewritten->bytes[0]==std::byte{0xAB});
  assert(buffers.RetainedBytes()==2*bytes);
  // The budget can be full and the CURRENT unchanged revision still valid.
  assert(buffers.Capture(object,address,bytes,ReadFake,&guest)==rewritten);
  W(guest,address,0x99887766);
  assert(!buffers.Capture(object,address,bytes,ReadFake,&guest));
  assert(rewritten->bytes[0]==std::byte{0xAB}); // no in-place mutation
  assert(buffers.RetainedBytes()==2*bytes);
  const auto failing=[](void*,uint32_t,std::byte* out,size_t) noexcept {
    out[0]=std::byte{0xFF};return false;
  };
  assert(!buffers.Capture(object,address,bytes,failing,&guest));
  assert(buffers.RetainedBytes()==2*bytes);
  buffers.Reset();
  assert(buffers.RetainedBytes()==0);
  // A queued GPU frame still owns the prior bytes after the next Present.
  assert(first->bytes[0]==std::byte{0x12} && rewritten->bytes[0]==std::byte{0xAB});
  const auto newFrame=buffers.Capture(object,address,bytes,ReadFake,&guest);
  assert(newFrame && newFrame->bytes[0]==std::byte{0x99});
  buffers.Reset();
  // Same contents at different addresses/objects are NOT resource identity.
  const auto owner=buffers.Capture(object,address,bytes,ReadFake,&guest);
  const auto other=buffers.Capture(object+4,address,bytes,ReadFake,&guest);
  assert(owner && other && owner!=other && other->guest_object==object+4);
  buffers.Reset();
  assert(!buffers.Capture(object,UINT32_MAX-3,bytes,ReadFake,&guest));
  assert(!buffers.Capture(object,address,5,ReadFake,&guest));
  assert(!buffers.Capture(object,address,(8u<<20)+1,ReadFake,&guest));
  assert(!buffers.Capture(object,address,bytes,nullptr,&guest));
  assert(!buffers.Capture(0,address,bytes,ReadFake,&guest));
  assert(!buffers.Capture(object,address,bytes,failing,&guest));
  assert(buffers.RetainedBytes()==0);
  const auto recovered=buffers.Capture(object,address,bytes,ReadFake,&guest);
  assert(recovered && recovered->bytes[0]==std::byte{0x99});
  // GPU streaming revisions must not exhaust the per-Present capture
  // budget once no original Draw owns their previous immutable contents.
  // The only owner of each prior revision is then the cache itself.
  OriginalGuestBufferSnapshots streaming(bytes);
  for(uint32_t revision=1;revision<=2048;++revision) {
    W(guest,address,revision);
    const auto current=streaming.Capture(
        object,address,bytes,ReadFake,&guest);
    assert(current && current->guest_size==bytes &&
           streaming.RetainedBytes()==bytes);
    const auto check=uint32_t(std::to_integer<uint8_t>(current->bytes[0]))<<24 |
                     uint32_t(std::to_integer<uint8_t>(current->bytes[1]))<<16 |
                     uint32_t(std::to_integer<uint8_t>(current->bytes[2]))<<8 |
                     uint32_t(std::to_integer<uint8_t>(current->bytes[3]));
    assert(check==revision);
    // The temporary current owner is destroyed at the end of each pass.
  }
  streaming.Reset();
  assert(streaming.RetainedBytes()==0);
}
}
int main() {
  CheckBoundVertexWindow();
  CheckIndexDmaFormats();
  // SHA-pinned USA PPC AOT verified one direct bl into DrawVertices and
  // five into DrawIndexedVertices. Tail branches do NOT create an LR:
  // never mistake their callsite+4 for an authenticated source owner.
  static_assert(ProvenUsaXdkDirectBlCaller(
      0x8288DF30u,0x82676430u)==0x82676400u);
  static_assert(ProvenUsaXdkDirectBlCaller(
      0x8288E318u,0x82640614u)==0x82640578u);
  static_assert(ProvenUsaXdkDirectBlCaller(
      0x8288E318u,0x8264164Cu)==0x82641618u);
  static_assert(ProvenUsaXdkDirectBlCaller(
      0x8288E318u,0x8265B53Cu)==0x8265B420u);
  static_assert(ProvenUsaXdkDirectBlCaller(
      0x8288E318u,0x82668184u)==0x826680B8u);
  static_assert(ProvenUsaXdkDirectBlCaller(
      0x8288E318u,0x8266899Cu)==0x826688D0u);
  static_assert(ProvenUsaXdkDirectBlCaller(
      0x8288E318u,0x826762BCu)==0u); // tail b, LR cannot be derived
  static_assert(ProvenUsaXdkDirectBlCaller(
      0x8288DF30u,0x826762E8u)==0u); // another original tail b
  static_assert(ProvenUsaXdkDirectBlCaller(
      0x8288E318u,0x82676430u)==0u); // incompatible XDK method
  static_assert(ProvenUsaXdkDirectBlCaller(
      0x8288DF30u,0x82640614u)==0u);
  static_assert(ProvenUsaXdkDirectBlCaller(0,0)==0u);
  // These are original EoR engine VIRTUAL shader calls, not guessed XDK
  // draw methods. Each method address was proven against a named original
  // shader .rdata vtable and a live shader instance in the private USA dump.
  static_assert(ProvenUsaEorShaderVtableForMethod(
      0x8265B420u)==0x82066504u); // Decal
  static_assert(ProvenUsaEorShaderVtableForMethod(
      0x82668D50u)==0x8206EBFCu); // TexturedRect
  static_assert(ProvenUsaEorShaderVtableForMethod(
      0x82669630u)==0x8206ECACu); // ScreenRect
  static_assert(ProvenUsaEorShaderVtableForMethod(
      0x8266A0C0u)==0x8206ED6Cu); // Font
  static_assert(ProvenUsaEorShaderVtableForMethod(
      0x8266CC28u)==0x8206F5DCu); // Sky
  static_assert(ProvenUsaEorShaderVtableForMethod(
      0x8266DDD0u)==0x8206F7CCu); // FlatShadow
  // 33 DIFFERENT original shader classes share the identical virtual
  // method address. Live original guest vtable, not method-only identity,
  // determines which class really owned the nested XDK Draw.
  static_assert(ProvenUsaEorShaderMethodEntry(0x826432F0u));
  static_assert(ProvenUsaEorShaderMethodVtablePair(
      0x826432F0u,0x82066294u)); // EBuildingWithWindowsShader
  static_assert(ProvenUsaEorShaderMethodVtablePair(
      0x826432F0u,0x8206636Cu)); // ESkyDomeShader
  static_assert(!ProvenUsaEorShaderMethodVtablePair(
      0x826432F0u,0x8206ED6Cu)); // EFontShader has distinct method
  static_assert(!ProvenUsaEorShaderMethodVtablePair(
      0x8266A0C0u,0x82066294u));
  static_assert(!ProvenUsaEorShaderMethodVtablePair(0,0));
  static_assert(!ProvenUsaEorShaderMethodVtablePair(
      0x826432F0u,0));
  static_assert(ProvenUsaEorShaderVtableForMethod(0x826432F0u)==0u);
  static_assert(ProvenUsaEorShaderVtableForMethod(
      0x8288E318u)==0u); // Original XDK function is not a shader instance
  static_assert(ProvenUsaEorShaderVtableForMethod(0u)==0u);
  CheckBufferSnapshots();
  // Actual USA Decal PPC at 0x8265B538 passes a geometry descriptor
  // discovered from its own render-item r28 through guest +0x3C,
  // then binds original XDK VB=geometry+0x18 and IB=geometry+0x38.
  // This direct original engine Draw ABI does NOT authorize material
  // effect/pass identity; only source guest geometry IDs are proven.
  RawDraw exact_decal{
      Kind::DrawIndexedVertices,0x8288E318u,0x8265B53Cu,
      {0x40002880u,4u,0u,0u,6u,0u,0u,0u}};
  exact_decal.observed_vs_matrices=OriginalVsMatrices{};
  exact_decal.observed_vs_matrices->original_guest_device=0x40002880u;
  exact_decal.observed_vs_matrices->
      original_vertex_buffer_guest_objects[0]=0xA0000018u;
  exact_decal.observed_vs_matrices->
      original_index_buffer_guest_object=0xA0000038u;
  OriginalEorShaderMethodAtDraw decal_owner{};
  decal_owner.original_shader_method_guest=0x8265B420u;
  decal_owner.original_shader_instance_guest=0xA3100010u;
  decal_owner.original_shader_vtable_guest=0x82066504u;
  decal_owner.nesting_depth=1;
  decal_owner.original_shader_method_vtable_verified=true;
  decal_owner.original_decal_render_item_guest=0xA3100F00u;
  decal_owner.original_decal_geometry_guest=0xA0000000u;
  decal_owner.original_decal_xdk_vb_ib_binding_verified=true;
  exact_decal.original_engine_shader_method=decal_owner;
  assert(ValidOriginalDecalGeometryBinding(exact_decal));
  // The genuine original 0x82642A88 calls shader vtable+44 at
  // bctrl 0x82642BD4. All 52 known original classes have one of 22
  // source method entrypoints, with 27 classes sharing 0x82642370.
  static_assert(ProvenUsaEorRenderItemMethodEntry(0x82642370u));
  static_assert(ProvenUsaEorRenderItemMethodVtablePair(
      0x82642370u,0x82066504u)); // Decal
  static_assert(ProvenUsaEorRenderItemMethodVtablePair(
      0x82642370u,0x8206ED6Cu)); // original EFontShader
  static_assert(ProvenUsaEorRenderItemMethodVtablePair(
      0x82663EF8u,0x82066074u)); // light outside
  static_assert(!ProvenUsaEorRenderItemMethodVtablePair(
      0x82642370u,0x82066074u)); // wrong class
  static_assert(!ProvenUsaEorRenderItemMethodVtablePair(
      0x8263DCD0u,0x82066504u)); // building-vs-Decal
  auto source_item=exact_decal;
  source_item.original_engine_shader_method->
      original_engine_item_method_guest=0x82642370u;
  source_item.original_engine_shader_method->
      original_engine_render_item_guest=0xA3100F00u;
  source_item.original_engine_shader_method->
      original_engine_render_item_context_guest=0xA3491120u;
  source_item.original_engine_shader_method->
      original_engine_render_item_vtable_guest=0x8206A100u;
  source_item.original_engine_shader_method->
      original_engine_render_item_material_guest=0xA3120010u;
  source_item.original_engine_shader_method->
      original_engine_item_material_source_verified=true;
  source_item.original_engine_shader_method->
      original_engine_item_abi_verified=true;
  assert(ValidOriginalEorSourceRenderItem(source_item));
  auto foreign_item=source_item;
  foreign_item.original_engine_shader_method->
      original_engine_item_method_guest=0x82663EF8u;
  assert(!ValidOriginalEorSourceRenderItem(foreign_item));
  foreign_item=source_item;
  foreign_item.original_engine_shader_method->
      original_engine_item_abi_verified=false;
  assert(!ValidOriginalEorSourceRenderItem(foreign_item));
  foreign_item=source_item;
  foreign_item.original_engine_shader_method->
      original_engine_render_item_guest=0u;
  assert(!ValidOriginalEorSourceRenderItem(foreign_item));
  foreign_item=source_item;
  foreign_item.original_engine_shader_method->
      original_engine_render_item_vtable_guest=0u;
  assert(!ValidOriginalEorSourceRenderItem(foreign_item));
  foreign_item=source_item;
  foreign_item.original_engine_shader_method->
      original_engine_render_item_material_guest=0u;
  assert(!ValidOriginalEorSourceRenderItem(foreign_item));
  foreign_item=source_item;
  foreign_item.original_engine_shader_method->
      original_engine_item_material_source_verified=false;
  assert(!ValidOriginalEorSourceRenderItem(foreign_item));
  // Real Decal source 0x82642C68 calls the original item virtual
  // material method+48, then source binder 0x825F8A50 with original
  // descriptor pointer in r4 at LR=0x82642CB4. The binder loads
  // descriptor+0 and +8; these words are NOT a proven effect pass.
  exact_decal.original_engine_shader_method->
      original_decal_material_descriptor_guest=0xA380B000u;
  exact_decal.original_engine_shader_method->
      original_decal_material_context_guest=0xA3100040u;
  exact_decal.original_engine_shader_method->
      original_decal_material_word0=0xA380B140u;
  exact_decal.original_engine_shader_method->
      original_decal_material_word8=0x00000004u;
  exact_decal.original_engine_shader_method->
      original_decal_item_material_binding_verified=true;
  assert(ValidOriginalDecalItemMaterialBinding(exact_decal));
  auto invalid_material=exact_decal;
  invalid_material.original_engine_shader_method->
      original_decal_item_material_binding_verified=false;
  assert(!ValidOriginalDecalItemMaterialBinding(invalid_material));
  invalid_material=exact_decal;
  invalid_material.original_engine_shader_method->
      original_decal_material_descriptor_guest=0u;
  assert(!ValidOriginalDecalItemMaterialBinding(invalid_material));
  invalid_material=exact_decal;
  invalid_material.original_engine_shader_method->
      original_decal_material_context_guest=0u;
  assert(!ValidOriginalDecalItemMaterialBinding(invalid_material));
  invalid_material=exact_decal;
  invalid_material.original_engine_shader_method->
      original_decal_xdk_vb_ib_binding_verified=false;
  assert(!ValidOriginalDecalItemMaterialBinding(invalid_material));
  // Original USA material 0x825F84C0 selects a 1-based source
  // state from the actual 92-byte table; indices at +84/+85 pick
  // unique stage entries in independent source VS/PS tables. Actual
  // 0x82874390/0x82874090 writes shader guest IDs to XDK device
  // +12696/+12692, independently observed on this SAME PPC Draw.
  auto pass=exact_decal;
  pass.original_engine_shader_method->
      original_pass_material_context_guest=0xA3100040u;
  pass.original_engine_shader_method->original_pass_state_one_based=2u;
  pass.original_engine_shader_method->
      original_pass_vertex_handle_entry_guest=0xA3102008u;
  pass.original_engine_shader_method->
      original_pass_pixel_handle_entry_guest=0xA3103010u;
  pass.original_engine_shader_method->
      original_pass_vs_xdk_guest_object=0xA3210000u;
  pass.original_engine_shader_method->
      original_pass_ps_xdk_guest_object=0xA3211000u;
  pass.original_engine_shader_method->
      original_pass_vs_ps_xdk_binding_verified=true;
  pass.observed_vs_matrices->original_vs_guest_object=0xA3210000u;
  pass.observed_vs_matrices->original_ps_guest_object=0xA3211000u;
  assert(ValidOriginalShaderPassStageBinding(pass));
  // Original 0x825F84C0 source material pass state has its own
  // 4-byte sampler STATE table at context+196; index/count at
  // state record +80 (BE16) and +87. Here source slot0 and slot3
  // have REAL guest XDK device+12544 texture objects at this Draw.
  pass.original_engine_shader_method->
      original_pass_sampler_slot_mask=(uint16_t(1u)<<0)|
                                      (uint16_t(1u)<<3);
  pass.original_engine_shader_method->
      original_pass_sampler_slots_verified=true;
  pass.original_engine_shader_method->
      original_pass_live_texture_objects_present=true;
  pass.observed_vs_matrices->original_texture_guest_objects[0]=0xA3331000u;
  pass.observed_vs_matrices->original_texture_guest_objects[3]=0xA3334000u;
  assert(ValidOriginalShaderPassSamplerStateAtDraw(pass));
  auto wrong_sampler=pass;
  wrong_sampler.observed_vs_matrices->
      original_texture_guest_objects[3]=0u;
  assert(!ValidOriginalShaderPassSamplerStateAtDraw(wrong_sampler));
  wrong_sampler=pass;
  wrong_sampler.original_engine_shader_method->
      original_pass_vs_ps_xdk_binding_verified=false;
  assert(!ValidOriginalShaderPassSamplerStateAtDraw(wrong_sampler));
  wrong_sampler=pass;
  wrong_sampler.original_engine_shader_method->
      original_pass_sampler_slots_verified=false;
  assert(!ValidOriginalShaderPassSamplerStateAtDraw(wrong_sampler));
  // A read-only source pass with no stated sampler-state table cannot
  // invent a proven texture fetch, even when stale guest textures linger.
  wrong_sampler=pass;
  wrong_sampler.original_engine_shader_method->
      original_pass_sampler_slot_mask=0;
  wrong_sampler.original_engine_shader_method->
      original_pass_live_texture_objects_present=false;
  assert(ValidOriginalShaderPassSamplerStateAtDraw(wrong_sampler));
  auto mismatched_pass=pass;
  mismatched_pass.observed_vs_matrices->
      original_ps_guest_object=0xA3211004u;
  assert(!ValidOriginalShaderPassStageBinding(mismatched_pass));
  mismatched_pass=pass;
  mismatched_pass.observed_vs_matrices->
      original_vs_guest_object=0xA3210004u;
  assert(!ValidOriginalShaderPassStageBinding(mismatched_pass));
  mismatched_pass=pass;
  mismatched_pass.original_engine_shader_method->
      original_pass_vs_ps_xdk_binding_verified=false;
  assert(!ValidOriginalShaderPassStageBinding(mismatched_pass));
  mismatched_pass=pass;
  mismatched_pass.original_engine_shader_method->
      original_pass_material_context_guest=0u;
  assert(!ValidOriginalShaderPassStageBinding(mismatched_pass));
  mismatched_pass=pass;
  mismatched_pass.original_engine_shader_method->
      original_pass_state_one_based=0u;
  assert(!ValidOriginalShaderPassStageBinding(mismatched_pass));
  auto incorrect_decal=exact_decal;
  incorrect_decal.observed_vs_matrices->
      original_index_buffer_guest_object=0xA0000040u;
  assert(!ValidOriginalDecalGeometryBinding(incorrect_decal));
  incorrect_decal=exact_decal;
  incorrect_decal.original_engine_shader_method->
      original_decal_geometry_guest=0xA0001000u;
  assert(!ValidOriginalDecalGeometryBinding(incorrect_decal));
  incorrect_decal=exact_decal;
  incorrect_decal.lr=0x8265B540u;
  assert(!ValidOriginalDecalGeometryBinding(incorrect_decal));
  incorrect_decal=exact_decal;
  incorrect_decal.original_engine_shader_method->
      original_decal_xdk_vb_ib_binding_verified=false;
  assert(!ValidOriginalDecalGeometryBinding(incorrect_decal));
  incorrect_decal=exact_decal;
  incorrect_decal.original_engine_shader_method->
      original_shader_method_guest=0x826432F0u;
  assert(!ValidOriginalDecalGeometryBinding(incorrect_decal));
  assert(VerifiedTriangleListCount(4u,3u)==3u);
  assert(VerifiedTriangleListCount(4u,6u)==6u);
  assert(VerifiedTriangleListCount(6u,4u)==6u);
  assert(VerifiedTriangleListCount(6u,3u)==3u);
  assert(OriginalTriangleFanVertex(0,0)==0u &&
         OriginalTriangleFanVertex(0,1)==1u &&
         OriginalTriangleFanVertex(0,2)==2u &&
         OriginalTriangleFanVertex(1,0)==0u &&
         OriginalTriangleFanVertex(1,1)==2u &&
         OriginalTriangleFanVertex(1,2)==3u);
  assert(!VerifiedTriangleListCount(5u,4u));
  assert(!VerifiedTriangleListCount(6u,2u));
  assert(!VerifiedTriangleListCount(6u,UINT32_MAX));

  // The original Hulk USA device shadow state has proven BE c7..c25 VS
  // registers and VS/PS guest object slots. Capture at the actual Draw.
  GuestMemory guest;
  // Original USA XDK ClearF captures the source memory BEFORE the actual
  // PPC method clears the guest targets: r5 RECT*, r6 float4*, f1=Z.
  const uint32_t clear_rect=0x100u,clear_color=0x140u;
  for(unsigned i=0;i<4;++i) {
    W(guest,clear_rect+4*i,(i%2==0 ? (i==0?0u:1280u) :
                                        (i==1?0u:720u)));
    W(guest,clear_color+4*i,
      std::bit_cast<uint32_t>(i==0?0.25f:i==1?0.5f:i==2?0.75f:1.f));
  }
  const std::array<uint32_t,8> clear_args{
      0x40002880u,0x31u,clear_rect,clear_color,0u,0x37u,0u,0u};
  const auto original_clear=CaptureOriginalXdkClearF(
      clear_args,0.625,ReadFake,&guest);
  assert(original_clear && original_clear->flags==0x31u &&
         original_clear->has_rect && original_clear->has_color &&
         original_clear->rect[2]==1280 &&
         original_clear->rect[3]==720 &&
         original_clear->color[0]==0.25f &&
         original_clear->color[2]==0.75f &&
         original_clear->depth==0.625f &&
         original_clear->stencil==0x37u);
  assert(IsOriginalFullTargetClear(*original_clear,1280,720));
  assert(!IsOriginalFullTargetClear(*original_clear,1920,1080));
  auto partial=*original_clear;
  partial.rect[2]=640;
  assert(!IsOriginalFullTargetClear(partial,1280,720));
  partial=*original_clear;
  partial.flags=0x02u; // native backend cannot assume RT1 is main RT0
  assert(!IsOriginalFullTargetClear(partial,1280,720));
  auto invalid_args=clear_args;
  invalid_args[3]=0;
  assert(!CaptureOriginalXdkClearF(invalid_args,0.625,ReadFake,&guest));
  invalid_args=clear_args;
  invalid_args[2]=0x7ffffff0u;
  assert(!CaptureOriginalXdkClearF(invalid_args,0.625,ReadFake,&guest));
  invalid_args=clear_args;
  invalid_args[1]=0x100u;
  assert(!CaptureOriginalXdkClearF(invalid_args,0.625,ReadFake,&guest));
  assert(!CaptureOriginalXdkClearF(clear_args,1.5,ReadFake,&guest));
  W(guest,clear_color,std::bit_cast<uint32_t>(
      std::numeric_limits<float>::quiet_NaN()));
  assert(!CaptureOriginalXdkClearF(clear_args,0.625,ReadFake,&guest));
  W(guest,clear_color,std::bit_cast<uint32_t>(0.25f));
  constexpr uint32_t device=0x2000;
  W(guest,device+12696,0x82ABCDEF);
  W(guest,device+12692,0x82FEDCBA);
  W(guest,device+11820,0x82345678);
  W(guest,device+12436,0x826789AB);
  W(guest,device+12460,0x82123456);
  W(guest,device+12460+15*4,0x82F01234);
  W(guest,device+12440,0x82888888);
  W(guest,device+12456,0x82999999);
  W(guest,device+12544,0x8200BEEF);
  W(guest,device+12548,0x8200BABE);
  W(guest,device+1920+11*16,std::bit_cast<uint32_t>(1.5f));
  W(guest,device+11856,1);
  const std::array<float,6> viewport{23,17,800,480,0.2f,0.7f};
  for(uint32_t i=0;i<viewport.size();++i) W(guest,device+12648u+4u*i,std::bit_cast<uint32_t>(viewport[i]));
  W(guest,device+12676,0xfffffffeu); W(guest,device+12684,811);
  const auto matrix=CaptureUsaVsMatrices(device,ReadFake,&guest);
  assert(matrix && matrix->raster && matrix->raster->scissor_enabled);
  assert(matrix->raster->x==23 && matrix->raster->width==800 && matrix->raster->height==480);
  assert(matrix->raster->min_depth==0.2f && matrix->raster->max_depth==0.7f);
  assert(matrix->raster->scissor[0]==-2 && matrix->raster->scissor[2]==811);
  assert(matrix && matrix->original_guest_device==device &&
         matrix->original_vs_guest_object==0x82ABCDEFu &&
         matrix->original_ps_guest_object==0x82FEDCBAu &&
         matrix->original_vertex_declaration_guest_object==0x82345678u &&
         matrix->original_index_buffer_guest_object==0x826789ABu &&
         matrix->original_vertex_buffer_guest_objects[0]==0x82123456u &&
         matrix->original_vertex_buffer_guest_objects[15]==0x82F01234u &&
         matrix->original_vertex_buffer_guest_objects[1]==0u &&
         matrix->original_rt0_guest_object==0x82888888u &&
         matrix->original_depth_guest_object==0x82999999u &&
         matrix->original_texture_guest_objects[0]==0x8200BEEFu &&
         matrix->original_texture_guest_objects[1]==0x8200BABEu);
  const auto view=ReadOriginalMatrix(*matrix,11,4);
  assert(view && view->original_register_count==4 &&
         view->values[0]==1.5f && view->values[15]==0.f);
  const auto world3=ReadOriginalMatrix(*matrix,7,3);
  assert(world3 && world3->original_register_count==3);
  assert(!ReadOriginalMatrix(*matrix,7,5));
  assert(!ReadOriginalMatrix(*matrix,23,4));
  W(guest,device+1920+7*16,
    std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN()));
  const auto nonfinite=CaptureUsaVsMatrices(device,ReadFake,&guest);
  assert(nonfinite && !ReadOriginalMatrix(*nonfinite,7,4) &&
         ReadOriginalMatrix(*nonfinite,11,4));
  assert(!CaptureUsaVsMatrices(0,ReadFake,&guest));
  assert(!CaptureUsaVsMatrices(UINT32_MAX,ReadFake,&guest));
  // The verified USA +1920 VS and +6016 PS banks contain 256 registers
  // EACH. Original pixel-lighting inputs must not be guessed from VS c7..25.
  W(guest,device+1920+255*16,std::bit_cast<uint32_t>(42.0f));
  W(guest,device+6016+0*16,std::bit_cast<uint32_t>(0.125f));
  W(guest,device+6016+127*16,std::bit_cast<uint32_t>(2.5f));
  W(guest,device+6016+255*16,std::bit_cast<uint32_t>(99.0f));
  OriginalShaderRegisters complete;
  assert(CaptureUsaAllShaderRegisters(device,ReadFake,&guest,complete));
  assert(complete.complete_float_banks);
  const uint32_t fetch0=device+1152,fetch15=fetch0+15*24;
  for(uint32_t i=0;i<16*6;++i)W(guest,fetch0+i*4,0);
  W(guest,fetch0,2);W(guest,fetch0+4,0xabcd006);
  W(guest,fetch15,2);W(guest,fetch15+20,0x451000); // Mip-only binding.
  W(guest,fetch0+24,2);W(guest,fetch0+28,0xe0000006); // Empty physical page after mask.
  CaptureUsaPixelFetches(device,ReadFake,&guest,complete);
  assert(complete.valid_pixel_fetch==0x8001);
  assert(complete.pixel_fetch[0][1]==0xabcd006 && complete.pixel_fetch[15][5]==0x451000);
  CaptureUsaPixelFetches(UINT32_MAX,ReadFake,&guest,complete);
  assert(!complete.valid_pixel_fetch);
  auto last_vs=ReadOriginalShaderFloat4(
      complete,OriginalShaderStage::Vertex,255);
  auto first_ps=ReadOriginalShaderFloat4(
      complete,OriginalShaderStage::Pixel,0);
  auto middle_ps=ReadOriginalShaderFloat4(
      complete,OriginalShaderStage::Pixel,127);
  auto last_ps=ReadOriginalShaderFloat4(
      complete,OriginalShaderStage::Pixel,255);
  assert(last_vs && (*last_vs)[0]==42.f);
  assert(first_ps && (*first_ps)[0]==0.125f);
  assert(middle_ps && (*middle_ps)[0]==2.5f);
  assert(last_ps && (*last_ps)[0]==99.f);
  assert(!ReadOriginalShaderFloat4(
      complete,OriginalShaderStage::Pixel,256));
  auto bad_reg=complete;
  bad_reg.finite_pixel[1]=0;
  assert(!ReadOriginalShaderFloat4(
      bad_reg,OriginalShaderStage::Pixel,127));
  W(guest,device+6016+7*16,
    std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN()));
  assert(CaptureUsaAllShaderRegisters(device,ReadFake,&guest,complete));
  assert(!ReadOriginalShaderFloat4(complete,OriginalShaderStage::Pixel,7));
  assert(ReadOriginalShaderFloat4(complete,OriginalShaderStage::Vertex,255));
  assert(!CaptureUsaAllShaderRegisters(UINT32_MAX,ReadFake,&guest,complete));
  assert(!CaptureUsaAllShaderRegisters(0,ReadFake,&guest,complete));

  FrameCapture capture;
  RawDraw a{Kind::DrawIndexedVertices,0x8288E318u,0x8263BF50u,{3,4,5,6,7,8,9,10}};
  RawDraw b{Kind::DrawVertices,0x8288DF30u,0x8263BF54u,
            {4,5,6,7,8,9,10,11}};
  a.observed_vs_matrices=*matrix;
  a.observed_shader_constants=std::make_shared<OriginalShaderRegisters>(complete);
  capture.Record(a);
  capture.Record(b);
  Frame first=capture.Finish();
  assert(first.sequence==1 && first.dropped==0);
  assert(first.calls.size()==2 && first.calls[0].function==0x8288E318u);
  assert(first.calls[1].gpr[7]==11);
  assert(first.calls[0].lr==0x8263BF50u);
  // A Draw-only list cannot become a native sole-presenter frame:
  // original RT/depth, clear and Resolve ownership MUST be ordered too.
  assert(first.commands.size()==2 &&
         first.commands[0].kind==CommandKind::DrawIndexedVertices &&
         first.commands[0].draw_ordinal==0 &&
         first.commands[1].draw_ordinal==1);
  assert(!ValidateOriginalCommandTimeline(first));
  FrameCapture chronology;
  chronology.RecordCommand(RawCommand{
      CommandKind::SetRenderTarget,0x8287F2C0u,0x8263A000u,
      {3,0,0x82001234u,0,0,0,0,0}});
  chronology.RecordCommand(RawCommand{
      CommandKind::SetDepthStencilSurface,0x8287F628u,0x8263A004u,
      {3,0x82004321u,0,0,0,0,0,0}});
  RawCommand checked_clear{CommandKind::ClearF,0x82888C40u,0x8263A008u,
      clear_args};
  checked_clear.captured_clear=*original_clear;
  chronology.RecordCommand(checked_clear);
  chronology.Record(a);
  chronology.RecordCommand(RawCommand{
      CommandKind::BeginTiling,0x8288A900u,0x8263A00Cu,
      {3,1,0,0,0,0,0,0}});
  chronology.Record(b);
  chronology.RecordCommand(RawCommand{
      CommandKind::EndTiling,0x8288AD98u,0x8263A010u,
      {3,1,0,0,0,0,0,0}});
  chronology.RecordCommand(RawCommand{
      CommandKind::Resolve,0x828897F8u,0x8263A014u,
      {3,1,0,0x82002000u,0,0,0,0}});
  chronology.RecordCommand(RawCommand{
      CommandKind::Present,0x82880778u,0x8263A018u,
      {3,0x82003000u,0,0,0,0,0,0}});
  const Frame full_timeline=chronology.Finish();
  assert(full_timeline.sequence==1 && full_timeline.calls.size()==2 &&
         full_timeline.commands.size()==9 &&
         ValidateOriginalCommandTimeline(full_timeline));
  // The original EoR shader-virtual-method call scope is a REAL
  // guest parent only after source vtable/this/entry identity validation.
  // It enriches the original XDK timeline without inventing the
  // independent material/effect/mesh resource witness.
  auto engine_source=full_timeline;
  OriginalEorShaderMethodAtDraw source{};
  source.original_shader_method_guest=0x8265B420u; // original Decal
  source.original_shader_vtable_guest=0x82066504u;
  source.original_shader_instance_guest=0xA38A2918u;
  source.source_r4_through_r7={1u,2u,3u,4u};
  source.nesting_depth=1;
  source.original_shader_method_vtable_verified=true;
  engine_source.calls[0].original_engine_shader_method=source;
  assert(ValidateOriginalCommandTimeline(engine_source));
  auto false_engine=engine_source;
  false_engine.calls[0].original_engine_shader_method->
      original_shader_vtable_guest=0x8206ED6Cu; // wrong class
  assert(!ValidateOriginalCommandTimeline(false_engine));
  false_engine=engine_source;
  false_engine.calls[0].original_engine_shader_method->
      original_shader_method_vtable_verified=false;
  assert(!ValidateOriginalCommandTimeline(false_engine));
  false_engine=engine_source;
  false_engine.calls[0].original_engine_shader_method->
      original_shader_method_guest=0x82000010u;
  false_engine.calls[0].original_engine_shader_method->
      original_shader_vtable_guest=0u;
  assert(!ValidateOriginalCommandTimeline(false_engine));
  auto shared_engine=engine_source;
  shared_engine.calls[0].original_engine_shader_method->
      original_shader_method_guest=0x826432F0u;
  shared_engine.calls[0].original_engine_shader_method->
      original_shader_vtable_guest=0x82066294u;
  assert(ValidateOriginalCommandTimeline(shared_engine));
  shared_engine.calls[0].original_engine_shader_method->
      original_shader_vtable_guest=0x8206ED6Cu;
  assert(!ValidateOriginalCommandTimeline(shared_engine));
  false_engine=engine_source;
  false_engine.calls[0].original_engine_shader_method->
      original_shader_instance_guest=0;
  assert(!ValidateOriginalCommandTimeline(false_engine));
  false_engine=engine_source;
  false_engine.calls[0].original_engine_shader_method->nesting_depth=0;
  assert(!ValidateOriginalCommandTimeline(false_engine));
  // A self-consistent copied timeline may still encode the WRONG PPC
  // function or invalid Draw ABI. Never authorize independent replay from
  // ordinal/argument equality alone.
  auto bad_draw_abi=full_timeline;
  bad_draw_abi.calls[0].function=0x8288E31Cu;
  bad_draw_abi.commands[3].function=0x8288E31Cu;
  assert(!ValidateOriginalCommandTimeline(bad_draw_abi));
  bad_draw_abi=full_timeline;
  bad_draw_abi.calls[1].lr=0;
  bad_draw_abi.commands[5].lr=0;
  assert(!ValidateOriginalCommandTimeline(bad_draw_abi));
  bad_draw_abi=full_timeline;
  bad_draw_abi.calls[0].gpr[4]=0; // missing indexed vertex count
  bad_draw_abi.commands[3].gpr[4]=0;
  assert(!ValidateOriginalCommandTimeline(bad_draw_abi));
  bad_draw_abi=full_timeline;
  bad_draw_abi.calls[1].gpr[2]=UINT32_MAX;
  bad_draw_abi.commands[5].gpr[2]=UINT32_MAX;
  assert(!ValidateOriginalCommandTimeline(bad_draw_abi));
  bad_draw_abi=full_timeline;
  bad_draw_abi.calls[1].kind=Kind::DrawVerticesUP;
  bad_draw_abi.commands[5].kind=CommandKind::DrawVerticesUP;
  assert(!ValidateOriginalCommandTimeline(bad_draw_abi));
  auto no_color_snapshot=full_timeline;
  no_color_snapshot.commands[2].captured_clear.reset();
  assert(!ValidateOriginalCommandTimeline(no_color_snapshot));
  auto invalid_timeline=full_timeline;
  std::swap(invalid_timeline.commands[3],invalid_timeline.commands[5]);
  assert(!ValidateOriginalCommandTimeline(invalid_timeline));
  invalid_timeline=full_timeline;
  invalid_timeline.commands[3].draw_ordinal=7;
  assert(!ValidateOriginalCommandTimeline(invalid_timeline));
  invalid_timeline=full_timeline;
  invalid_timeline.commands.erase(invalid_timeline.commands.begin()+3);
  assert(!ValidateOriginalCommandTimeline(invalid_timeline));
  invalid_timeline=full_timeline;
  invalid_timeline.dropped_commands=1;
  assert(!ValidateOriginalCommandTimeline(invalid_timeline));
  invalid_timeline=full_timeline;
  invalid_timeline.commands[8].kind=CommandKind::ClearF;
  assert(!ValidateOriginalCommandTimeline(invalid_timeline));
  bool rejected_unknown_command=false;
  try {
    chronology.RecordCommand(RawCommand{CommandKind::ClearF,0x82888D68u});
  } catch(const std::invalid_argument&) {rejected_unknown_command=true;}
  assert(rejected_unknown_command); // original outer Clear nests ClearF
  assert(first.calls[0].observed_vs_matrices &&
         ReadOriginalMatrix(*first.calls[0].observed_vs_matrices,11,4)
             ->values[0]==1.5f);
  assert(first.calls[0].observed_shader_constants &&
         ReadOriginalShaderFloat4(
             *first.calls[0].observed_shader_constants,
             OriginalShaderStage::Pixel,255)->at(0)==99.0f);
  // Exact original XDK PPC signature: r3=device, r4=primitive,
  // r5=signed base vertex, r6=start index, r7=index count.
  RawDraw live{Kind::DrawIndexedVertices,0x8288E318u,0x8263BF50u,
               {0x40002880u,4u,0xFFFFFFFEu,12u,6u,0u,0u,0u}};
  const auto typed=DecodeOriginalIndexedXdkCall(live);
  assert(typed && typed->guest_device==0x40002880u &&
         typed->primitive==4u && typed->base_vertex==-2 &&
         typed->first_index==12 && typed->index_count==6);
  live.kind=Kind::DrawVertices;
  assert(!DecodeOriginalIndexedXdkCall(live));
  live.kind=Kind::DrawIndexedVertices;
  live.function=0x8288DF30u;
  assert(!DecodeOriginalIndexedXdkCall(live));
  live.function=0x8288E318u;
  live.gpr[3]=UINT32_MAX;
  assert(!DecodeOriginalIndexedXdkCall(live));
  live.gpr[3]=12u;
  live.gpr[4]=0;
  assert(!DecodeOriginalIndexedXdkCall(live));
  RawDraw vertex{Kind::DrawVertices,0x8288DF30u,0x8263BF50u,
                 {0x40002880u,4u,21u,6u,0u,0u,0u,0u}};
  const auto nonindexed=DecodeOriginalVerticesXdkCall(vertex);
  assert(nonindexed && nonindexed->guest_device==0x40002880u &&
         nonindexed->primitive==4u && nonindexed->start_vertex==21u &&
         nonindexed->vertex_count==6u);
  vertex.kind=Kind::DrawIndexedVertices;
  assert(!DecodeOriginalVerticesXdkCall(vertex));
  vertex.kind=Kind::DrawVertices;
  vertex.gpr[2]=UINT32_MAX;
  assert(!DecodeOriginalVerticesXdkCall(vertex));
  CompletedFrameMailbox mailbox;
  mailbox.Publish(first);
  mailbox.Publish(Frame{42,0,{a}});
  assert(mailbox.TotalOverwritten()==1);
  auto delivered=mailbox.Take();
  assert(delivered && delivered->frame.sequence==42 &&
         delivered->overwritten_since_take==1 &&
         delivered->frame.calls.size()==1);
  assert(!mailbox.Take());
  mailbox.Publish(Frame{43,0,{a,b}});
  delivered=mailbox.Take();
  assert(delivered && delivered->frame.sequence==43 &&
         delivered->overwritten_since_take==0);
  mailbox.Reset();
  assert(mailbox.TotalOverwritten()==0 && !mailbox.Take());
  bool missing_sequence=false;
  try {mailbox.Publish(Frame{});}
  catch(const std::invalid_argument&){missing_sequence=true;}
  assert(missing_sequence);
  mailbox.Publish(Frame{55,0,{a}});
  mailbox.Publish(Frame{56,0,{a,b}});
  mailbox.Publish(Frame{57,0,{a}});
  assert(mailbox.TotalOverwritten()==2);
  auto newest=mailbox.Take();
  assert(newest && newest->frame.sequence==57 &&
         newest->overwritten_since_take==2 &&
         newest->frame.calls.size()==1);
  assert(!mailbox.Take());
  // Preflight preserves the pending original frame until a matching
  // sequence is atomically claimed. A concurrent newer Present wins.
  mailbox.Reset();
  mailbox.Publish(Frame{61,0,{a}});
  {
    const auto preview=mailbox.Peek();
    assert(preview && preview->sequence==61);
    assert(mailbox.MissedSinceTake()==0);
    assert(!mailbox.TakeIfSequence(61)); // preview still owned
    assert(!mailbox.TakeIfSequence(60)); // wrong original sequence
  }
  auto admitted=mailbox.TakeIfSequence(61);
  assert(admitted && admitted->frame.sequence==61 && !mailbox.Take());
  mailbox.Publish(Frame{70,0,{a}});
  auto stale=mailbox.Peek();
  mailbox.Publish(Frame{71,0,{a,b}});
  assert(stale && stale->sequence==70 && mailbox.MissedSinceTake()==1);
  stale.reset();
  assert(!mailbox.TakeIfSequence(70));
  auto latest=mailbox.TakeIfSequence(71);
  assert(latest && latest->frame.sequence==71 &&
         latest->overwritten_since_take==1 &&
         latest->frame.calls.size()==2);
  mailbox.Reset();
  // Native mixed-frame staging owns a complete immutable epoch before
  // expensive PSO/material work, while newer Presents may continue.
  mailbox.Publish(Frame{75,0,{a,b}});
  const auto inspected=mailbox.Peek();
  assert(!mailbox.ClaimCleanIfSequence(74));
  const auto claimed=mailbox.ClaimCleanIfSequence(75);
  assert(claimed && claimed==inspected && !mailbox.Peek());
  mailbox.Publish(Frame{76,0,{a}});
  assert(claimed->sequence==75 && claimed->calls.size()==2);
  assert(mailbox.Peek()->sequence==76 && mailbox.MissedSinceTake()==0);
  mailbox.Publish(Frame{77,0,{b}});
  assert(!mailbox.ClaimCleanIfSequence(77));
  assert(mailbox.DiscardIfSequence(77));
  mailbox.Reset();
  // Worker/render-thread loss recovery: discard the overwritten epoch
  // without accidentally authorizing its newest frame, then accept the
  // next independent original Present with a clean loss counter.
  mailbox.Publish(Frame{80,0,{a}});
  mailbox.Publish(Frame{81,0,{a,b}});
  assert(mailbox.MissedSinceTake()==1);
  auto discarded_epoch=mailbox.TakeIfSequence(81);
  assert(discarded_epoch && discarded_epoch->overwritten_since_take==1 &&
         !mailbox.Peek() && mailbox.MissedSinceTake()==0);
  mailbox.Publish(Frame{82,0,{a}});
  auto recovered_epoch=mailbox.TakeIfSequence(82);
  assert(recovered_epoch && recovered_epoch->frame.sequence==82 &&
         recovered_epoch->overwritten_since_take==0);
  // An old read-only preview can still be inspected after its discarded
  // Present, without permanently poisoning the next native Vulkan frame.
  mailbox.Reset();
  mailbox.Publish(Frame{90,0,{a}});
  mailbox.Publish(Frame{91,0,{a,b}});
  const auto held_preview=mailbox.Peek();
  assert(held_preview && held_preview->sequence==91);
  assert(mailbox.MissedSinceTake()==1);
  assert(!mailbox.DiscardIfSequence(90)); // stale epoch must not erase 91
  assert(mailbox.DiscardIfSequence(91));  // immutable preview still owned
  assert(held_preview->sequence==91 && !mailbox.Peek());
  assert(mailbox.MissedSinceTake()==0);
  mailbox.Publish(Frame{92,0,{a}});
  const auto next_epoch=mailbox.TakeIfSequence(92);
  assert(next_epoch && next_epoch->frame.sequence==92 &&
         next_epoch->overwritten_since_take==0);
  mailbox.Reset();
  assert(capture.Finish().sequence==2);
  assert(capture.Finish().calls.empty());
  for(size_t i=0;i<FrameCapture::kMaxDraws+3;++i)capture.Record(a);
  Frame full=capture.Finish();
  assert(full.calls.size()==FrameCapture::kMaxDraws && full.dropped==3);
  capture.Record(b);
  capture.Reset();
  assert(capture.Finish().calls.empty());
  bool caught=false;
  try { capture.Record(RawDraw{}); }
  catch(const std::invalid_argument&) { caught=true; }
  assert(caught);
  // Real EoR ABI discovery needs *repeatable callsite observations*, not
  // an assumption that PPC r3..r10 encode host Vulkan buffer handles.
  XdkCallsiteEvidence evidence;
  Frame a1{1,0,{a,a,b}};
  evidence.Observe(a1);
  auto hot=evidence.HotCallsites();
  assert(hot.size()==2 && hot[0].xdk_function==a.function);
  assert(hot[0].caller_lr==a.lr && hot[0].recorded_draws==2);
  assert(hot[0].frames_seen==1 && !hot[0].varying_gpr_mask);
  RawDraw moving=a;
  moving.gpr[0]=20;
  moving.gpr[4]=0;
  evidence.Observe(Frame{2,3,{moving,a}});
  hot=evidence.HotCallsites(1);
  assert(hot.size()==1 && hot[0].recorded_draws==4);
  assert(hot[0].frames_seen==2 && hot[0].first_frame==1 &&
         hot[0].last_frame==2 && hot[0].varying_gpr_mask==(1u<<0 | 1u<<4));
  assert(evidence.DroppedDraws()==3 && evidence.UntrackedCalls()==0);
  bool outOfOrder=false;
  try { evidence.Observe(Frame{2,0,{a}}); }
  catch(const std::invalid_argument&) { outOfOrder=true; }
  assert(outOfOrder);
  bool tooMany=false;
  try { (void)evidence.HotCallsites(XdkCallsiteEvidence::kMaxCallsites+1); }
  catch(const std::invalid_argument&) { tooMany=true; }
  assert(tooMany);
  evidence.Reset();
  assert(evidence.HotCallsites().empty() && evidence.DroppedDraws()==0);
}
