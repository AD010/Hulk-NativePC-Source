#include "../native_port/src/eor_native_draw_geometry.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace hulk::eor;
namespace {
struct Fake final : model::NativeMeshUploadBackend {
  uint64_t next=0;
  uint64_t UploadPositions(std::span<const model::NativePosition>) override {return ++next;}
  uint64_t UploadOriginalVertexStream(std::span<const std::byte>) override {return ++next;}
  uint64_t UploadIndices(std::span<const uint32_t>) override {return ++next;}
  void ReleaseBuffer(uint64_t) noexcept override {}
};
model::LoadedNativeGeometry MakeModel() {
  model::LoadedNativeGeometry m;
  m.model.original_resource_hash=0xCAFE;
  m.model.source.platform=model::Platform::Xbox360;
  m.model.source.model_name="city_building";
  m.model.source.surface_records=1;
  m.model.bytes.resize(128);
  model::SurfaceMaterials surf;
  surf.ordinal=0;surf.original_material_name="brick";
  surf.material_data_offset=22;
  m.original_surfaces.push_back(surf);
  model::NativeIndexedMesh mesh;
  mesh.source_header_offset=40;mesh.source_indices_offset=67;
  mesh.source_vertices_offset=73;mesh.original_vertex_stride=24;
  mesh.original_attribute_count=3;mesh.original_attribute_declarations.resize(3);
  mesh.positions={{0,0,0},{1,0,0},{0,1,0}};
  mesh.triangle_list_indices={0,1,2};mesh.original_vertex_stream.resize(72);
  m.decoded_meshes.push_back(std::move(mesh));
  return m;
}
void I32(std::vector<uint8_t>& b,uint32_t x){for(int i=0;i<4;++i)b.push_back(uint8_t(x>>(8*i)));}
aot::EffectCatalog Catalog(){
  std::vector<uint8_t> b{'H','E','O','R','M','A','T','2'};
  I32(b,1);I32(b,2);I32(b,0x1234);I32(b,0);
  for(int i=0;i<32;++i)b.push_back(0x11);
  for(int i=0;i<32;++i)b.push_back(0x22);
  const auto file=std::filesystem::temp_directory_path()/"hulk_draw_geometry_effect_test.bin";
  {std::ofstream out(file,std::ios::binary|std::ios::trunc);
   out.write(reinterpret_cast<const char*>(b.data()),b.size());}
  aot::EffectCatalog effects;effects.Load(file);
  std::filesystem::remove(file);
  return effects;
}
template<class F> void Reject(F&& f){bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}assert(failed);}
}
int main(){
  Fake gpu;
  auto resident=model::UploadOriginalModel(MakeModel(),gpu);
  const auto effects=Catalog();
  std::vector<const model::UploadedOriginalModel*> models{&resident};
  native::VerifiedDrawSource original;
  original.original_model_hash=0xCAFE;
  original.original_mesh_header_offset=40;
  original.original_surface_ordinal=0;original.original_surface_name="brick";
  original.original_effect_hash=0x1234;original.original_pass_index=0;
  original.vertex_source_sha256.fill(0x11);original.pixel_source_sha256.fill(0x22);
  original.vertex_shader_id=101;original.pixel_shader_id=202;
  original.index_count=3;
  original.engine_draw_verified=original.mesh_material_verified=true;
  original.host_vertex_layout_verified=original.render_state_verified=true;
  // One independently verified material texture ownership descriptor.
  gpu::VerifiedEorSampler valid_sampler;
  valid_sampler.original_model_hash=0xCAFE;
  valid_sampler.original_surface_ordinal=0;
  valid_sampler.original_effect_hash=0x1234;
  valid_sampler.original_pass_index=0;
  valid_sampler.original_texture_hash=0xBEEF;
  valid_sampler.original_fetch_register=0;
  valid_sampler.stage=gpu::OriginalSamplerStage::Pixel;
  valid_sampler.exact_shader_variable_name="g_Diffuse";
  valid_sampler.original_material_ownership_verified=true;
  valid_sampler.original_shader_fetch_slot_verified=true;
  valid_sampler.original_texture_format_and_mips_verified=true;
  original.original_sampler_slots={valid_sampler};
  const auto good=native::BindVerifiedGeometryDraws(
     std::span<const native::VerifiedDrawSource>(&original,1),models,effects);
  assert(good.size()==1 && good[0].original_samplers.size()==1 &&
         good[0].original_samplers[0].original_texture_hash==0xBEEF &&
         good[0].original_model_hash==0xCAFE &&
         good[0].original_surface_ordinal==0 &&
         good[0].draw.base_vertex==0);
  assert(good.size()==1 && good[0].draw.vertex_buffer==1 &&
         good[0].draw.index_buffer==3 && good[0].draw.index_count==3 &&
         good[0].effect_hash==0x1234 && good[0].pass_index==0);
  const auto check=[&](auto mutate){auto copy=original;mutate(copy);
    Reject([&]{std::vector<native::VerifiedDrawSource> batch{original,copy};
               (void)native::BindVerifiedGeometryDraws(batch,models,effects);});};
  check([](auto& v){v.original_model_hash=0xBEEF;});
  check([](auto& v){v.original_mesh_header_offset=99;});
  check([](auto& v){v.original_surface_ordinal=1;});
  check([](auto& v){v.original_surface_name="wrong";});
  check([](auto& v){v.original_effect_hash=0xABCD;});
  check([](auto& v){v.original_pass_index=1;});
  check([](auto& v){v.pixel_source_sha256.fill(0x33);});
  check([](auto& v){v.first_index=6;});
  check([](auto& v){v.index_count=9;});
  check([](auto& v){v.index_count=2;});
  check([](auto& v){v.host_base_vertex=1;});
  check([](auto& v){v.host_base_vertex=-1;});
  check([](auto& v){v.vertex_shader_id=0;});
  check([](auto& v){v.engine_draw_verified=false;});
  check([](auto& v){v.mesh_material_verified=false;});
  check([](auto& v){v.host_vertex_layout_verified=false;});
  check([](auto& v){v.render_state_verified=false;});
  check([](auto& v){v.original_sampler_slots[0].original_texture_hash=0;});
  check([](auto& v){v.original_sampler_slots[0].original_surface_ordinal=1;});
  check([](auto& v){v.original_sampler_slots[0].original_shader_fetch_slot_verified=false;});
  Reject([&]{std::vector<const model::UploadedOriginalModel*> dup{&resident,&resident};
            (void)native::BindVerifiedGeometryDraws(
                std::span<const native::VerifiedDrawSource>(&original,1),dup,effects);});
  Reject([&]{std::vector<const model::UploadedOriginalModel*> nil{nullptr};
            (void)native::BindVerifiedGeometryDraws(
                std::span<const native::VerifiedDrawSource>(&original,1),nil,effects);});
  // Synthetic values, actual USA DrawIndexedVertices PPC argument layout.
  // Guest index origin 120 differs from local index origin 0 after rebase.
  xdk::RawDraw ppc{xdk::Kind::DrawIndexedVertices,0x8288E318u,0x8263BF50u,
                   {0x40002880u,4u,0xFFFFFFFEu,120u,3u,0u,0u,0u}};
  const auto typed=xdk::DecodeOriginalIndexedXdkCall(ppc);
  assert(typed && typed->base_vertex==-2 && typed->first_index==120);
  native::VerifiedXdkIndexedSubmission verified{};
  verified.original_guest_call=*typed;
  verified.native_draw=original;
  verified.original_triangle_list_verified=true;
  verified.guest_to_native_index_stream_verified=true;
  verified.original_material_at_draw_verified=true;
  const auto observed=xdk::Frame{1,0,{ppc}};
  const auto matched=native::MatchCapturedXdkDraws(observed,
    std::span<const native::VerifiedXdkIndexedSubmission>(&verified,1));
  assert(matched.size()==1 && matched[0].original_model_hash==0xCAFE &&
         matched[0].first_index==0 && matched[0].host_base_vertex==0);
  auto altered=verified;
  altered.original_guest_call.first_index+=1;
  Reject([&]{(void)native::MatchCapturedXdkDraws(observed,
    std::span<const native::VerifiedXdkIndexedSubmission>(&altered,1));});
  altered=verified;altered.original_guest_call.base_vertex=0;
  Reject([&]{(void)native::MatchCapturedXdkDraws(observed,
    std::span<const native::VerifiedXdkIndexedSubmission>(&altered,1));});
  altered=verified;altered.guest_to_native_index_stream_verified=false;
  Reject([&]{(void)native::MatchCapturedXdkDraws(observed,
    std::span<const native::VerifiedXdkIndexedSubmission>(&altered,1));});
  Reject([&]{(void)native::MatchCapturedXdkDraws(xdk::Frame{1,1,{ppc}},
    std::span<const native::VerifiedXdkIndexedSubmission>(&verified,1));});
  ppc.kind=xdk::Kind::DrawVertices;
  Reject([&]{(void)native::MatchCapturedXdkDraws(xdk::Frame{2,0,{ppc}},
    std::span<const native::VerifiedXdkIndexedSubmission>(&verified,1));});
  assert(gpu.next==3);
}
