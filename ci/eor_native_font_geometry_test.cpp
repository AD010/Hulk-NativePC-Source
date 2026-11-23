// Source-only native font geometry regression. No copyrighted UI resources.
// Synthetic vertices exercise real XDK DrawVertices/fan/index semantics,
// authenticated EFontShader and atlas, and native float4/index32 uploads.
#include "../native_port/src/eor_native_font_geometry.h"
#include <cassert>
#include <cstring>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
using namespace hulk::eor;

namespace {
void Word(std::vector<std::byte>& bytes,uint32_t value) {
  for(int shift:{24,16,8,0})
    bytes.push_back(std::byte((value>>shift)&255u));
}
void Vertex(std::vector<std::byte>& bytes,float x,float y,float u,float v) {
  Word(bytes,std::bit_cast<uint32_t>(x));
  Word(bytes,std::bit_cast<uint32_t>(y));
  Word(bytes,std::bit_cast<uint32_t>(0.f));
  Word(bytes,std::bit_cast<uint32_t>(u));
  Word(bytes,std::bit_cast<uint32_t>(v));
  for(unsigned n=0;n<4;++n)bytes.push_back(std::byte(n==0?255:64));
}
template<class F> void Rejected(F f) {
  bool rejected=false;
  try {f();}catch(const std::exception&){rejected=true;}
  assert(rejected);
}
struct FakeUpload final:model::NativeMeshUploadBackend {
  uint64_t next=1;
  std::vector<uint64_t> freed;
  uint64_t UploadPositions(std::span<const model::NativePosition>)override{return ++next;}
  uint64_t UploadOriginalVertexStream(std::span<const std::byte>)override{return ++next;}
  uint64_t UploadDecodedAttributes(std::span<const model::HostAttribute> v)override {
    assert(!v.empty());return ++next;
  }
  uint64_t UploadIndices(std::span<const uint32_t> ids)override {
    assert(ids.size()%3==0);return ++next;
  }
  void ReleaseBuffer(uint64_t value) noexcept override {freed.push_back(value);}
};
}
int main() {
  xdk::RawDraw call{};
  call.kind=xdk::Kind::DrawVertices;
  call.function=0x8288DF30u;
  // The original shader-class entry is NOT an XDK DrawVertices call
  // site. Use the independently proved USA 6534 source BL return address.
  call.lr=0x82676430u;
  call.gpr[0]=0x83001000u; // guest device
  call.gpr[1]=6u; // original triangle fan
  call.gpr[2]=0u; // first vertex
  call.gpr[3]=4u; // vertex count
  xdk::OriginalEorShaderMethodAtDraw parent{};
  parent.original_shader_method_guest=0x8266A0C0u;
  parent.original_shader_vtable_guest=0x8206ED6Cu;
  parent.original_shader_instance_guest=0x82090000u;
  parent.original_shader_method_vtable_verified=true;
  parent.original_pass_vs_ps_xdk_binding_verified=true;
  parent.original_pass_sampler_slots_verified=true;
  parent.original_pass_live_texture_objects_present=true;
  parent.original_pass_sampler_slot_mask=1u;
  parent.original_pass_vs_xdk_guest_object=0x83000100u;
  parent.original_pass_ps_xdk_guest_object=0x83000200u;
  parent.nesting_depth=1u;
  call.original_engine_shader_method=parent;
  xdk::OriginalVsMatrices device{};
  device.original_guest_device=call.gpr[0];
  device.original_vertex_declaration_guest_object=0x83000300u;
  device.original_vs_guest_object=parent.original_pass_vs_xdk_guest_object;
  device.original_ps_guest_object=parent.original_pass_ps_xdk_guest_object;
  device.original_vertex_buffer_guest_objects[0]=0x83000400u;
  device.original_texture_guest_objects[0]=0x83000500u;
  device.original_index_buffer_guest_object=0x83000600u;
  call.observed_vs_matrices=device;
  call.original_pixel_texture_keys[0]=0xDAD1DD22u;
  call.observed_live_eor_effect_pass=aot::OriginalEffectPass{0xDAD10001u,0u};
  auto vb=std::make_shared<xdk::OriginalGuestBufferBytes>();
  vb->guest_object=device.original_vertex_buffer_guest_objects[0];
  vb->guest_address=0x40001000u;
  Vertex(vb->bytes,0.f,0.f,0.f,0.f);
  Vertex(vb->bytes,2.f,0.f,1.f,0.f);
  Vertex(vb->bytes,2.f,1.f,1.f,1.f);
  Vertex(vb->bytes,0.f,1.f,0.f,1.f);
  vb->guest_size=static_cast<uint32_t>(vb->bytes.size());
  call.source_vb0=vb;
  ui::VerifiedFontVertexLayout layout{};
  layout.original_vertex_declaration_guest=device.original_vertex_declaration_guest_object;
  layout.original_stride=24;
  layout.position={0,3,model::AttributeEncoding::BigFloat32};
  layout.texcoord={12,2,model::AttributeEncoding::BigFloat32};
  layout.color=model::VerifiedAttributeLayout{20,4,model::AttributeEncoding::UNorm8};
  layout.original_pixel_texture_slot=0;
  layout.original_font_atlas_hash=call.original_pixel_texture_keys[0];
  layout.original_vertex_declaration_layout_verified=true;
  layout.original_font_sampler_owner_verified=true;
  const auto f=ui::DecodeOriginalLiveFontDraw(call,layout,21,4);
  auto shifted=call;
  auto prefixed=std::make_shared<xdk::OriginalGuestBufferBytes>(*call.source_vb0);
  prefixed->bytes.insert(prefixed->bytes.begin(),12,std::byte{0xff});
  prefixed->guest_size+=12;
  shifted.source_vb0=prefixed;
  shifted.captured_vertex_bindings=true;
  shifted.vertex_bindings[0]=xdk::UsaVertexStreamBinding{
      prefixed->guest_object,prefixed->guest_address,prefixed->guest_size,12,
      prefixed->guest_size-12,layout.original_stride};
  auto offset_glyph=ui::DecodeOriginalLiveFontDraw(shifted,layout,21,4);
  assert(offset_glyph.positions==f.positions && offset_glyph.texcoords==f.texcoords &&
         offset_glyph.triangle_indices==f.triangle_indices);
  shifted.vertex_bindings[0]->length+=4;
  Rejected([&]{(void)ui::DecodeOriginalLiveFontDraw(shifted,layout,21,4);});
  assert(f.original_frame_sequence==21 && f.original_draw_ordinal==4 &&
         f.original_shader_instance_guest==parent.original_shader_instance_guest);
  assert(f.positions.size()==4 && f.texcoords.size()==4 && f.colors.size()==4);
  assert(f.triangle_indices==std::vector<uint32_t>({0,1,2,0,2,3}));
  assert(f.positions[2][0]==2.f && f.positions[2][1]==1.f);
  assert(f.texcoords[2][0]==1.f && f.texcoords[2][1]==1.f);
  assert(f.colors[0][0]==1.f);
  FakeUpload gpu;
  {
    auto uploaded=ui::UploadOriginalLiveFontDraw(f,gpu);
    assert(uploaded.vertex_count==4 && uploaded.index_count==6 &&
           uploaded.position_handle && uploaded.texcoord_handle &&
           uploaded.color_handle && uploaded.index_handle);
  }
  assert(gpu.freed.size()==4);
  // Verify exact original indexed EFont triangle list, without UP or an
  // invented archive model owner. Guest BE index encoding is source-proven.
  auto indexed=call;
  indexed.kind=xdk::Kind::DrawIndexedVertices;
  indexed.function=0x8288E318u;
  indexed.lr=0x82668184u; // confirmed original DrawIndexedVertices BL site
  indexed.gpr[1]=4u;
  indexed.gpr[2]=0u;
  indexed.gpr[3]=0u;
  indexed.gpr[4]=6u;
  auto ib=std::make_shared<xdk::OriginalGuestBufferBytes>();
  ib->guest_object=device.original_index_buffer_guest_object;
  ib->guest_address=0x40002000u;
  for(uint8_t n:{0u,1u,2u,0u,2u,3u}) {
    ib->bytes.push_back(std::byte{0});
    ib->bytes.push_back(std::byte(n));
  }
  ib->guest_size=static_cast<uint32_t>(ib->bytes.size());
  indexed.source_ib=ib;
  auto indexed_layout=layout;
  indexed_layout.original_index_encoding_verified=true;
  indexed_layout.original_index_element_bytes=2;
  auto g=ui::DecodeOriginalLiveFontDraw(indexed,indexed_layout,22,0);
  assert(g.triangle_indices==std::vector<uint32_t>({0,1,2,0,2,3}));
  Rejected([&]{(void)ui::DecodeOriginalLiveFontDraw(indexed,layout,22,0);});
  indexed_layout.original_index_big_endian=false;
  Rejected([&]{(void)ui::DecodeOriginalLiveFontDraw(indexed,indexed_layout,22,0);});
  auto captured=indexed;
  captured.captured_vertex_bindings=true;
  captured.vertex_bindings[0]=xdk::UsaVertexStreamBinding{
      vb->guest_object,vb->guest_address,vb->guest_size,0,vb->guest_size,layout.original_stride};
  captured.index_binding=xdk::UsaIndexBufferBinding{
      ib->guest_object,ib->guest_address,ib->guest_size,2,1};
  assert(ui::DecodeOriginalLiveFontDraw(captured,layout,22,0).triangle_indices==g.triangle_indices);
  Rejected([&]{(void)ui::DecodeOriginalLiveFontDraw(captured,indexed_layout,22,0);});
  auto swapped=std::make_shared<xdk::OriginalGuestBufferBytes>(*ib);
  for(size_t i=0;i<ib->bytes.size();++i)swapped->bytes[i^2u]=ib->bytes[i];
  captured.source_ib=swapped;captured.index_binding->endian=2;
  assert(ui::DecodeOriginalLiveFontDraw(captured,layout,22,0).triangle_indices==g.triangle_indices);
  captured.index_binding->bytes-=2;
  Rejected([&]{(void)ui::DecodeOriginalLiveFontDraw(captured,layout,22,0);});
  auto wrong=call;wrong.original_pixel_texture_keys[0]^=1;
  Rejected([&]{(void)ui::DecodeOriginalLiveFontDraw(wrong,layout,22,0);});
  wrong=call;wrong.original_engine_shader_method->original_shader_vtable_guest=0x8206EBFCu;
  Rejected([&]{(void)ui::DecodeOriginalLiveFontDraw(wrong,layout,22,0);});
  wrong=call;wrong.source_vb0.reset();
  Rejected([&]{(void)ui::DecodeOriginalLiveFontDraw(wrong,layout,22,0);});
  wrong=call;wrong.gpr[1]=5u;
  Rejected([&]{(void)ui::DecodeOriginalLiveFontDraw(wrong,layout,22,0);});
  wrong=call;
  wrong.original_engine_shader_method->original_pass_vs_ps_xdk_binding_verified=false;
  Rejected([&]{(void)ui::DecodeOriginalLiveFontDraw(wrong,layout,22,0);});
  // Original font emits BeginVertices/EndVertices with a transient stream.
  // Keep slot 0 bound to an unrelated VB to catch accidental reuse.
  auto in=call;
  in.kind=xdk::Kind::EndVertices; in.function=0x8288D8F8; in.lr=0x82676344;
  in.source_vb0.reset(); in.source_ib.reset();
  constexpr uint32_t dev=0x100,wrapper=0x4000,sp=0x5000,instance=0x6000,
                     decl=0x6300,allocation=0x7000;
  in.gpr[0]=dev;
  in.original_engine_shader_method->original_shader_instance_guest=instance;
  in.observed_vs_matrices->original_guest_device=dev;
  in.observed_vs_matrices->original_vertex_declaration_guest_object=decl;
  in.observed_live_eor_effect_pass=aot::OriginalEffectPass{0xe601e566u,0};
  std::vector<std::byte> mem(0x8000);
  const auto write=[&](uint32_t addr,uint32_t value) {
    for(unsigned i=0;i<4;++i)mem.at(addr+i)=std::byte(value>>(24-8*i));
  };
  write(sp,sp+96);write(sp+88,0x8266A0B8);write(sp+176,2);
  write(wrapper,dev);write(wrapper+12,instance+200);
  write(wrapper+28,13);write(wrapper+32,8);write(wrapper+36,24);
  write(instance+200,decl);write(decl+24,3);
  constexpr uint32_t elements[]={0,0x2A23B9,0,12,0x182886,0xA0000,16,0x2C23A5,0x50000};
  for(unsigned i=0;i<9;++i)write(decl+52+4*i,elements[i]);
  write(dev+13448,allocation);write(dev+13456,48);
  write(allocation+12,0xff804020u);
  auto read=[](void* source,uint32_t addr,std::byte* dest,size_t n) noexcept {
    auto& data=*static_cast<std::vector<std::byte>*>(source);
    if(uint64_t(addr)+n>data.size())return false;
    std::memcpy(dest,data.data()+addr,n);return true;
  };
  assert(!xdk::CaptureUsaFontFetch(dev,read,&mem));
  write(dev+1152,2);write(dev+1156,0x32100006);write(dev+1164,0x00550c14);
  const auto fetch=xdk::CaptureUsaFontFetch(dev,read,&mem);
  assert(fetch && (*fetch)[1]==0x32100006 && (*fetch)[3]==0x00550c14);
  write(dev+1164,0x00100c14);
  assert((*fetch)[3]!=(*xdk::CaptureUsaFontFetch(dev,read,&mem))[3]);
  assert(!xdk::CaptureUsaFontFetch(UINT32_MAX,read,&mem));
  xdk::OriginalGuestBufferSnapshots snapshots;
  auto capture=[&]{return xdk::CaptureUsaInlineGlyphs(in,wrapper,sp,read,&mem,snapshots);};
  in.inline_glyphs=capture();
  assert(in.inline_glyphs && in.inline_glyphs->vertex_count==8);
  auto source_layout=ui::ResolveUsaInlineFontLayout(in);
  assert(source_layout && source_layout->original_stride==24);
  auto glyphs=ui::DecodeOriginalLiveFontDraw(in,*source_layout,22,3);
  assert(glyphs.triangle_indices==std::vector<uint32_t>({0,1,2,0,2,3,4,5,6,4,6,7}));
  assert(glyphs.colors[0]==model::HostAttribute({128/255.f,64/255.f,32/255.f,1.f}));
  write(decl+68,0);assert(!capture());write(decl+68,0x182886);
  write(sp+88,0);assert(!capture());write(sp+88,0x8266A0B8);
  write(dev+13456,47);assert(!capture());write(dev+13456,48);
  write(allocation+12,0xff0000ff);auto next=capture();
  assert(next && next->payload!=in.inline_glyphs->payload);
  in.inline_glyphs.reset();assert(!ui::ResolveUsaInlineFontLayout(in));
  xdk::FrameCapture collector;collector.Record(in);
  collector.RecordCommand({xdk::CommandKind::Present,0x82880778});
  assert(!xdk::ValidateOriginalCommandTimeline(collector.Finish()));
  auto bad_layout=layout;bad_layout.original_font_sampler_owner_verified=false;
  Rejected([&]{(void)ui::DecodeOriginalLiveFontDraw(call,bad_layout,22,0);});
}
