#include "../native_port/src/eor_native_draw_geometry.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
// Leave the exact failing original Draw/provenance assertion visible in
// Windows GitHub Actions instead of silently returning a generic exit 3.
#undef assert
#define assert(check) do { \
  if (!(check)) { \
    std::fprintf(stderr,"[HULK/EOR-DRAW-TEST] %s:%d: %s\\n", \
                 __FILE__,__LINE__,#check); \
    std::fflush(stderr); \
    std::abort(); \
  } \
} while(false)
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
  surf.indexed_texture_references.push_back({0xBEEFu,56u,{0xBEEFu,512u,64u,0u}});
  m.original_surfaces.push_back(surf);
  model::NativeIndexedMesh mesh;
  mesh.source_header_offset=40;mesh.source_indices_offset=67;
  mesh.source_vertices_offset=79;mesh.original_vertex_stride=24;
  mesh.original_attribute_count=3;mesh.original_attribute_declarations.resize(3);
  mesh.positions={{0,0,0},{1,0,0},{0,1,0},
                  {2,0,0},{3,0,0},{2,1,0}};
  mesh.triangle_list_indices={0,1,2,3,4,5};
  mesh.original_vertex_stream.resize(144);
  m.decoded_meshes.push_back(std::move(mesh));
  return m;
}
model::LoadedNativeGeometry MakeFanModel() {
  auto m=MakeModel();
  auto& mesh=m.decoded_meshes.at(0);
  mesh.positions={{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
  mesh.triangle_list_indices={0,1,2,0,2,3};
  mesh.original_vertex_stream.resize(4u*24u);
  return m;
}
void I32(std::vector<uint8_t>& b,uint32_t x){for(int i=0;i<4;++i)b.push_back(uint8_t(x>>(8*i)));}
aot::EffectCatalog Catalog(bool another=false){
  std::vector<uint8_t> b{'H','E','O','R','M','A','T','2'};
  I32(b,another?2u:1u);I32(b,2);I32(b,0x1234);I32(b,0);
  for(int i=0;i<32;++i)b.push_back(0x11);
  for(int i=0;i<32;++i)b.push_back(0x22);
  if(another) {
    I32(b,0x4567);I32(b,0);
    for(int i=0;i<32;++i)b.push_back(0x33);
    for(int i=0;i<32;++i)b.push_back(0x44);
  }
  const auto file=std::filesystem::temp_directory_path()/"hulk_draw_geometry_effect_test.bin";
  {std::ofstream out(file,std::ios::binary|std::ios::trunc);
   out.write(reinterpret_cast<const char*>(b.data()),b.size());}
  aot::EffectCatalog effects;effects.Load(file);
  std::filesystem::remove(file);
  return effects;
}
template<class F> void Reject(F&& f){bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}assert(failed);}

void AddMinimalProgram(std::vector<uint8_t>& bytes,uint8_t id,uint32_t stage) {
  for(unsigned i=0;i<32;++i)bytes.push_back(id);
  std::vector<uint32_t> spirv={
      0x07230203u,0x10500u,0u,16u,0u,
      0x00020011u,1u,0x0003000eu,0u,1u,
      0x0005000fu,stage==1?0u:4u,1u,0x6e69616du,0u};
  if(stage==2) {
    // The synthetic pixel material below actually binds g_Diffuse. Its
    // shader fixture must declare that image too; do not weaken production
    // shader/material validation to accept a sampler absent from SPIR-V.
    const uint32_t image[]={
      0x00030016u,2u,32u,                 // OpTypeFloat
      0x00090019u,3u,2u,1u,0u,0u,0u,1u,0u, // OpTypeImage 2D
      0x00040020u,4u,0u,3u,               // pointer UniformConstant
      0x0004003bu,4u,5u,0u,               // OpVariable
      0x00040047u,5u,33u,0u,              // Binding 0
      0x00040047u,5u,34u,0u,              // DescriptorSet 0
      0x00050005u,5u,0x69445f67u,0x73756666u,0x65u}; // g_Diffuse
    spirv.insert(spirv.end(),std::begin(image),std::end(image));
  }
  I32(bytes,stage);
  I32(bytes,static_cast<uint32_t>(spirv.size()));
  for(uint32_t word:spirv)I32(bytes,word);
}
aot::ShaderBank MinimalPrograms() {
  std::vector<uint8_t> data{'H','E','O','R','S','P','V','1'};
  I32(data,2);I32(data,2);
  AddMinimalProgram(data,0x11,1);
  AddMinimalProgram(data,0x22,2);
  const auto file=std::filesystem::temp_directory_path()/
                  "hulk_draw_geometry_timeline_spirv.bin";
  {std::ofstream out(file,std::ios::binary|std::ios::trunc);
   out.write(reinterpret_cast<const char*>(data.data()),data.size());}
  aot::ShaderBank bank;
  bank.Load(file);
  std::filesystem::remove(file);
  return bank;
}
aot::ReflectionBank MinimalReflections() {
  std::vector<uint8_t> data{'H','E','O','R','R','E','F','1'};
  I32(data,2);I32(data,1);
  for(uint32_t stage=1;stage<=2;++stage) {
    for(unsigned i=0;i<32;++i)data.push_back(stage==1?0x11:0x22);
    I32(data,stage);
    // Synthetic test-only shaders have no original camera registers.
    for(unsigned i=0;i<4;++i){I32(data,UINT32_MAX);I32(data,0);}
  }
  const auto file=std::filesystem::temp_directory_path()/
                  "hulk_draw_geometry_timeline_reflection.bin";
  {std::ofstream out(file,std::ios::binary|std::ios::trunc);
   out.write(reinterpret_cast<const char*>(data.data()),data.size());}
  aot::ReflectionBank reflection;
  reflection.Load(file);
  std::filesystem::remove(file);
  return reflection;
}
struct TimelineGpu final:aot::ShaderBindingBackend {
  unsigned begins=0,draws=0,resolves=0,composites=0,aborts=0,sampler_binds=0;
  std::vector<xdk::CommandKind> events;
  bool CanReplayOriginalXdkTimeline(const xdk::Frame& frame) const override {
    return xdk::ValidateOriginalCommandTimeline(frame);
  }
  bool CanReplayOriginalXdkCommand(const xdk::RawCommand&) const override {
    return true;
  }
  bool ReplayOriginalXdkCommand(const xdk::RawCommand& cmd) override {
    events.push_back(cmd.kind);
    return true;
  }
  bool BeginScene(native::Size,uint32_t) override {++begins;return true;}
  void AbortScene() noexcept override {++aborts;}
  bool BindOriginalSurface(uint32_t,uint32_t) override {return true;}
  bool BindOriginalEffect(uint32_t,uint32_t) override {return true;}
  bool BindOriginalConstants(const aot::ReflectedShader&,
                             const aot::ReflectedShader&) override {return true;}
  bool BindOfflineShaders(std::span<const uint32_t>,
                          std::span<const uint32_t>) override {return true;}
  bool BindOriginalSamplers(
      std::span<const gpu::VerifiedEorSampler> samplers) override {
    assert(samplers.size()==1 && samplers[0].original_texture_hash==0xBEEF &&
           samplers[0].exact_shader_variable_name=="g_Diffuse" &&
           samplers[0].stage==gpu::OriginalSamplerStage::Pixel);
    ++sampler_binds;
    return true;
  }
  bool DrawIndexed(const native::IndexedDraw&) override {++draws;return true;}
  bool ResolveScene(native::Size) override {++resolves;return true;}
  bool Composite(native::Rect,native::Size) override {++composites;return true;}
};
}

int main(){
  try {
  Fake gpu;
  auto resident=model::UploadOriginalModel(MakeModel(),gpu);
  const auto effects=Catalog();
  std::vector<const model::UploadedOriginalModel*> models{&resident};
  native::VerifiedDrawSource original;
  original.original_model_hash=0xCAFE;
  original.original_mesh_header_offset=40;
  original.original_surface_ordinal=0;original.original_surface_name="brick";
  {
    std::string transient_name="brick";
    original.original_surface_name=transient_name;
    transient_name.assign("not_the_original_material");
    assert(original.original_surface_name=="brick");
  }
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
  const auto identified=native::IdentifyOriginalMaterialSurface(
      resident,original.original_sampler_slots);
  assert(identified && *identified==0);
  auto bad_texture=original.original_sampler_slots;
  bad_texture[0].original_texture_hash=0xFEEDu;
  assert(!native::IdentifyOriginalMaterialSurface(resident,bad_texture));
  const auto good=native::BindVerifiedGeometryDraws(
     std::span<const native::VerifiedDrawSource>(&original,1),models,effects);
  assert(good.size()==1 && good[0].original_samplers.size()==1 &&
         good[0].original_samplers[0].original_texture_hash==0xBEEF &&
         good[0].original_model_hash==0xCAFE &&
         good[0].original_surface_ordinal==0 &&
         good[0].original_mesh_header_offset==40 &&
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
  check([](auto& v){v.host_base_vertex=4;});
  check([](auto& v){v.host_base_vertex=-1;});
  // Different indexed windows can legitimately use different baseVertex
  // offsets on the same 6-vertex buffer.
  auto pos=original;
  pos.host_base_vertex=1;
  const auto posDraw=native::BindVerifiedGeometryDraws(
      std::span<const native::VerifiedDrawSource>(&pos,1),models,effects);
  assert(posDraw.size()==1 && posDraw[0].draw.base_vertex==1);
  // Triangle-list count must be a multiple of three, but an index
  // window can begin at any position in a shared, authenticated IB.
  auto offset=original;
  offset.first_index=1;
  const auto offsetDraw=native::BindVerifiedGeometryDraws(
      std::span<const native::VerifiedDrawSource>(&offset,1),models,effects);
  assert(offsetDraw.size()==1 && offsetDraw[0].draw.first_index==1);
  auto neg=original;
  neg.first_index=3;neg.host_base_vertex=-3;
  const auto negDraw=native::BindVerifiedGeometryDraws(
      std::span<const native::VerifiedDrawSource>(&neg,1),models,effects);
  assert(negDraw.size()==1 && negDraw[0].draw.base_vertex==-3);
  check([](auto& v){v.vertex_shader_id=0;});
  check([](auto& v){v.engine_draw_verified=false;});
  check([](auto& v){v.mesh_material_verified=false;});
  check([](auto& v){v.host_vertex_layout_verified=false;});
  check([](auto& v){v.render_state_verified=false;});
  check([](auto& v){v.original_sampler_slots[0].original_texture_hash=0;});
  check([](auto& v){v.original_sampler_slots[0].original_surface_ordinal=1;});
  check([](auto& v){v.original_sampler_slots[0].original_shader_fetch_slot_verified=false;});
  auto twoSurfaces=MakeModel();
  twoSurfaces.model.source.surface_records=2;
  auto secondSurface=twoSurfaces.original_surfaces[0];
  secondSurface.ordinal=1;
  secondSurface.original_material_name="metal";
  secondSurface.material_data_offset=60;
  secondSurface.indexed_texture_references[0].original_texture_hash=0xFEEDu;
  secondSurface.indexed_texture_references[0].original_resource.hash=0xFEEDu;
  twoSurfaces.original_surfaces.push_back(secondSurface);
  Fake twoGpu;
  auto twoOwner=model::UploadOriginalModel(std::move(twoSurfaces),twoGpu);
  std::vector<const model::UploadedOriginalModel*> twoModels{&twoOwner};
  auto wrongMaterial=original;
  wrongMaterial.original_surface_ordinal=1;
  wrongMaterial.original_surface_name="metal";
  wrongMaterial.original_sampler_slots[0].original_surface_ordinal=1;
  Reject([&]{(void)native::BindVerifiedGeometryDraws(
      std::span<const native::VerifiedDrawSource>(&wrongMaterial,1),
      twoModels,effects);});
  auto indistinguishable=MakeModel();
  indistinguishable.model.source.surface_records=2;
  auto clone=indistinguishable.original_surfaces[0];
  clone.ordinal=1;clone.original_material_name="metal";
  clone.material_data_offset=60;
  indistinguishable.original_surfaces.push_back(clone);
  Fake duplicateGpu;
  auto ambiguous=model::UploadOriginalModel(std::move(indistinguishable),duplicateGpu);
  assert(!native::IdentifyOriginalMaterialSurface(
      ambiguous,original.original_sampler_slots));
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
  xdk::OriginalVsMatrices real_at_draw{};
  real_at_draw.original_guest_device=0x40002880u;
  real_at_draw.finite_register_mask=(1u<<19)-1u;
  real_at_draw.c7_through_c25[(11-7)*4]=2.0f;
  ppc.observed_vs_matrices=real_at_draw;
  auto complete_constants=std::make_shared<xdk::OriginalShaderRegisters>();
  complete_constants->finite_vertex[0]|=uint64_t(1)<<11;
  complete_constants->vertex[11*4]=2.f;
  ppc.observed_shader_constants=complete_constants;
  const auto observed=xdk::Frame{1,0,{ppc}};
  const auto matched=native::MatchCapturedXdkDraws(observed,
    std::span<const native::VerifiedXdkIndexedSubmission>(&verified,1));
  assert(matched.size()==1 && matched[0].original_model_hash==0xCAFE &&
         matched[0].first_index==0 && matched[0].host_base_vertex==0 &&
         matched[0].original_frame_sequence==1 &&
         matched[0].original_call_ordinal==0 &&
         matched[0].original_vs_matrices &&
         matched[0].original_shader_constants==complete_constants &&
         xdk::ReadOriginalMatrix(*matched[0].original_vs_matrices,11,4)
            ->values[0]==2.0f);
  const auto tagged=native::BindVerifiedGeometryDraws(matched,models,effects);
  assert(tagged.size()==1 && tagged[0].draw.source_frame==1 &&
         tagged[0].draw.source_ordinal==0 &&
         tagged[0].original_mesh_header_offset==40 &&
         tagged[0].draw.guest_vs_matrices &&
         tagged[0].draw.guest_shader_constants==complete_constants &&
         xdk::ReadOriginalMatrix(*tagged[0].draw.guest_vs_matrices,11,4)
            ->values[0]==2.0f);
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
  // Capturing a PPC XDK Draw doesn't authorize treating strips/lines
  // or non-triangle counts as an original host triangle list.
  auto non_triangle=observed;
  non_triangle.calls[0].gpr[1]=3u;
  Reject([&]{(void)native::MatchCapturedXdkDraws(non_triangle,
    std::span<const native::VerifiedXdkIndexedSubmission>(&verified,1));});
  non_triangle=observed;
  non_triangle.calls[0].gpr[4]=4u;
  Reject([&]{(void)native::MatchCapturedXdkDraws(non_triangle,
    std::span<const native::VerifiedXdkIndexedSubmission>(&verified,1));});
  Reject([&]{(void)native::MatchCapturedXdkDraws(xdk::Frame{1,1,{ppc}},
    std::span<const native::VerifiedXdkIndexedSubmission>(&verified,1));});
  ppc.kind=xdk::Kind::DrawVertices;
  Reject([&]{(void)native::MatchCapturedXdkDraws(xdk::Frame{2,0,{ppc}},
    std::span<const native::VerifiedXdkIndexedSubmission>(&verified,1));});
  // Genuine DrawVertices (PPC r3..r6) can be lowered to the actual
  // indexed host triangle list only when every vertex is sequential.
  xdk::RawDraw plain{xdk::Kind::DrawVertices,0x8288DF30u,0x8263BF50u,
                     {0x40002880u,4u,30u,3u,0u,0u,0u,0u}};
  const auto vertexCall=xdk::DecodeOriginalVerticesXdkCall(plain);
  assert(vertexCall);
  native::VerifiedXdkVerticesSubmission nonindexed{};
  nonindexed.original_guest_call=*vertexCall;
  nonindexed.native_draw=original;
  nonindexed.native_draw.first_index=3;
  nonindexed.original_triangle_list_verified=true;
  nonindexed.guest_to_native_vertex_stream_verified=true;
  nonindexed.original_material_at_draw_verified=true;
  nonindexed.host_first_vertex=3;
  std::vector<native::VerifiedXdkDrawSubmission> mixed{verified,nonindexed};
  const xdk::Frame mixedFrame{2,0,{observed.calls[0],plain}};
  const auto nativeMixed=native::MatchCapturedMixedDraws(mixedFrame,mixed,models);
  assert(nativeMixed.size()==2 && nativeMixed[0].original_call_ordinal==0 &&
         nativeMixed[1].original_call_ordinal==1 &&
         nativeMixed[1].first_index==3 &&
         nativeMixed[1].original_model_hash==0xCAFE &&
         !nativeMixed[0].original_linear_draw_verified &&
         nativeMixed[1].original_linear_draw_verified);
  auto invalidNonIndexed=nonindexed;
  invalidNonIndexed.host_first_vertex=2;
  mixed[1]=invalidNonIndexed;
  Reject([&]{(void)native::MatchCapturedMixedDraws(mixedFrame,mixed,models);});
  invalidNonIndexed=nonindexed;
  invalidNonIndexed.original_triangle_list_verified=false;
  mixed[1]=invalidNonIndexed;
  Reject([&]{(void)native::MatchCapturedMixedDraws(mixedFrame,mixed,models);});
  // Original USA 195.log records 22 menu DrawVertices calls from
  // 0x8268BFE4, primitive 6, four source vertices (two fan triangles).
  Fake fanUploader;
  auto fanResident=model::UploadOriginalModel(MakeFanModel(),fanUploader);
  std::vector<const model::UploadedOriginalModel*> fanModels{&fanResident};
  xdk::RawDraw fan{xdk::Kind::DrawVertices,0x8288DF30u,0x8268BFE4u,
      {0x40002880u,6u,0u,4u,0u,0u,0u,0u}};
  const auto fanCall=xdk::DecodeOriginalVerticesXdkCall(fan);
  assert(fanCall && xdk::VerifiedTriangleListCount(
      fanCall->primitive,fanCall->vertex_count)==6u);
  auto fanSubmission=nonindexed;
  fanSubmission.original_guest_call=*fanCall;
  fanSubmission.native_draw.first_index=0;
  fanSubmission.native_draw.index_count=6;
  fanSubmission.host_first_vertex=0;
  const native::VerifiedXdkDrawSubmission fanVariant{fanSubmission};
  const auto fanFrame=xdk::Frame{8,0,{fan}};
  const auto fanMatched=native::MatchCapturedMixedDraws(
      fanFrame,std::span<const native::VerifiedXdkDrawSubmission>(
          &fanVariant,1),fanModels);
  assert(fanMatched.size()==1 && fanMatched[0].original_linear_draw_verified &&
         fanMatched[0].index_count==6);
  const auto fanGpuDraw=native::BindVerifiedGeometryDraws(
      fanMatched,fanModels,effects);
  assert(fanGpuDraw.size()==1 && fanGpuDraw[0].draw.index_count==6 &&
         fanGpuDraw[0].source_nonindexed_linearization_verified);
  auto badFanSource=MakeFanModel();
  badFanSource.decoded_meshes[0].triangle_list_indices[3]=2;
  Fake badFanUploader;
  auto badFan=model::UploadOriginalModel(std::move(badFanSource),badFanUploader);
  std::vector<const model::UploadedOriginalModel*> badFanModels{&badFan};
  Reject([&]{(void)native::MatchCapturedMixedDraws(
      fanFrame,std::span<const native::VerifiedXdkDrawSubmission>(
          &fanVariant,1),badFanModels);});
  auto unsupportedStrip=fanFrame;
  unsupportedStrip.calls[0].gpr[1]=5u;
  Reject([&]{(void)native::MatchCapturedMixedDraws(
      unsupportedStrip,std::span<const native::VerifiedXdkDrawSubmission>(
          &fanVariant,1),fanModels);});
  auto wrongFanCount=fanSubmission;
  wrongFanCount.native_draw.index_count=3;
  const native::VerifiedXdkDrawSubmission wrongCountVariant{wrongFanCount};
  Reject([&]{(void)native::MatchCapturedMixedDraws(
      fanFrame,std::span<const native::VerifiedXdkDrawSubmission>(
          &wrongCountVariant,1),fanModels);});
  // A missing original UP/inline geometry cannot be passed as DrawVertices.
  mixed[1]=nonindexed;
  auto up=plain;up.kind=xdk::Kind::DrawVerticesUP;
  Reject([&]{(void)native::MatchCapturedMixedDraws(
      xdk::Frame{2,0,{observed.calls[0],up}},mixed,models);});
  // Real guest GPU identities independently crosswalk original XDK
  // objects to the original MODELS/ShaderEffects/sampler source witness.
  auto runtime=observed;
  auto live_state=*runtime.calls[0].observed_vs_matrices;
  live_state.original_vs_guest_object=0x82001111u;
  live_state.original_ps_guest_object=0x82002222u;
  live_state.original_vertex_declaration_guest_object=0x82003333u;
  live_state.original_index_buffer_guest_object=0x82004444u;
  live_state.original_vertex_buffer_guest_objects[0]=0x82008888u;
  live_state.original_rt0_guest_object=0x82005555u;
  live_state.original_depth_guest_object=0x82006666u;
  live_state.original_texture_guest_objects[0]=0x82007777u;
  runtime.calls[0].observed_vs_matrices=live_state;
  native::VerifiedXdkResourceWitness witness{};
  witness.original_resources=native::ResourcesAtDraw(live_state);
  witness.original_source_submission=verified;
  witness.source_verified_texture_slot_mask=1u;
  witness.source_verified_vertex_stream_slot_mask=1u;
  witness.original_xdk_vertex_stream_slot_mapping_verified=true;
  witness.original_xdk_texture_slot_mapping_verified=true;
  witness.original_guest_resource_to_material_mapping_verified=true;
  std::vector<native::VerifiedXdkResourceWitness> catalog{witness};
  // Real ShaderClass -> original XDK Draw identity, sourced from the
  // independently validated original PPC virtual method and vtable.
  // Attaching its parent to an ALREADY independent material witness
  // must not change the authenticated mesh/effect/surface claims.
  auto shaderParentRuntime=runtime;
  xdk::OriginalEorShaderMethodAtDraw engineParent{};
  engineParent.original_shader_method_guest=0x8265B420u; // Decal
  engineParent.original_shader_instance_guest=0xA38A2918u;
  engineParent.original_shader_vtable_guest=0x82066504u;
  engineParent.source_r4_through_r7={11u,22u,33u,44u};
  engineParent.nesting_depth=1u;
  engineParent.original_shader_method_vtable_verified=true;
  shaderParentRuntime.calls[0].original_engine_shader_method=engineParent;
  const auto originalScopes=native::AttachLiveEorShaderMethodParents(
      shaderParentRuntime,catalog);
  assert(originalScopes.size()==1 &&
         originalScopes[0].original_source_engine_shader_method &&
         originalScopes[0].original_source_engine_shader_method->
             original_shader_instance_guest==0xA38A2918u &&
         originalScopes[0].original_guest_resource_to_material_mapping_verified);
  auto parentBound=native::ResolveCapturedResourceDraws(
      shaderParentRuntime,originalScopes,models);
  assert(parentBound.size()==1 && parentBound[0].original_model_hash==0xCAFE);
  // The same material witness WITHOUT original shader entry ownership
  // must never attach to a genuinely observed different EoR class Draw.
  Reject([&]{(void)native::ResolveCapturedResourceDraws(
      shaderParentRuntime,catalog,models);});
  auto wrongParent=originalScopes;
  wrongParent[0].original_source_engine_shader_method->
      original_shader_vtable_guest=0x8206ED6Cu;
  Reject([&]{(void)native::AttachLiveEorShaderMethodParents(
      shaderParentRuntime,wrongParent);});
  Reject([&]{(void)native::ResolveCapturedResourceDraws(
      shaderParentRuntime,wrongParent,models);});
  auto unknownEngine=shaderParentRuntime;
  unknownEngine.calls[0].original_engine_shader_method->
      original_shader_method_guest=0x82000010u;
  unknownEngine.calls[0].original_engine_shader_method->
      original_shader_vtable_guest=0u;
  Reject([&]{(void)native::AttachLiveEorShaderMethodParents(
      unknownEngine,catalog);});
  auto missingParent=shaderParentRuntime;
  missingParent.calls[0].original_engine_shader_method.reset();
  Reject([&]{(void)native::AttachLiveEorShaderMethodParents(
      missingParent,originalScopes);});
  // Guest XDK GPU byte identity is matched directly to the ORIGINAL model
  // stream, not inferred from a shared shader or an EoR CRC32 name hash.
  auto realModel=MakeModel();
  auto vb=std::make_shared<xdk::OriginalGuestBufferBytes>();
  vb->guest_object=live_state.original_vertex_buffer_guest_objects[0];
  vb->guest_address=0x40010000;
  vb->bytes=realModel.decoded_meshes[0].original_vertex_stream;
  vb->guest_size=uint32_t(vb->bytes.size());
  auto ib=std::make_shared<xdk::OriginalGuestBufferBytes>();
  ib->guest_object=live_state.original_index_buffer_guest_object;
  ib->guest_address=0x40012000;
  for(uint32_t idx:realModel.decoded_meshes[0].triangle_list_indices) {
    ib->bytes.push_back(std::byte(idx>>8));
    ib->bytes.push_back(std::byte(idx&255u));
  }
  ib->guest_size=uint32_t(ib->bytes.size());
  auto realBufferDraw=runtime.calls[0];
  // The older synthetic Draw uses firstIndex=120 and baseVertex=-2 to
  // exercise rebase arithmetic; this separate REAL-buffer proof checks
  // only a physically valid local firstIndex/baseVertex window.
  realBufferDraw.gpr[2]=0;
  realBufferDraw.gpr[3]=0;
  realBufferDraw.source_vb0=vb;
  realBufferDraw.source_ib=ib;
  const auto realMesh=native::IdentifyExactOriginalGuestMesh(realBufferDraw,models);
  assert(realMesh && realMesh->model_hash==0xCAFE &&
         realMesh->mesh_header_offset==40 &&
         realMesh->original_index_big_endian &&
         realMesh->guest_first_vertex==0 &&
         realMesh->guest_first_index==0 &&
         realMesh->original_index_element_bytes==2);
  // Two endian interpretations of ONE physically identical original
  // 16-bit index stream are not two EoR mesh owners. Degenerate source
  // triangles can contain all-zero indices, yielding the same bytes in
  // both orders. The matcher must keep the authentic mesh rather than
  // rejecting it as an ambiguous unrelated model.
  auto endianAliasModel=MakeModel();
  endianAliasModel.decoded_meshes[0].triangle_list_indices.assign(6u,0u);
  Fake endianGpu;
  auto endianResident=model::UploadOriginalModel(
      std::move(endianAliasModel),endianGpu);
  const std::vector<const model::UploadedOriginalModel*> endianModels{
      &endianResident};
  auto endianIb=std::make_shared<xdk::OriginalGuestBufferBytes>(*ib);
  endianIb->bytes.assign(12u,std::byte{0});
  endianIb->guest_size=static_cast<uint32_t>(endianIb->bytes.size());
  auto aliasDraw=realBufferDraw;
  aliasDraw.source_ib=endianIb;
  const auto alias=native::IdentifyExactOriginalGuestMesh(
      aliasDraw,endianModels);
  assert(alias && alias->model_hash==0xCAFE &&
         alias->original_index_element_bytes==2 &&
         alias->original_index_big_endian);
  // Two separately resident copies ARE genuinely ambiguous, even if
  // their bytes and archive hashes happen to be the same.
  const std::vector<const model::UploadedOriginalModel*> duplicatedAlias{
      &endianResident,&endianResident};
  assert(!native::IdentifyExactOriginalGuestMesh(aliasDraw,duplicatedAlias));
  // A typical XDK resource allocation packs multiple original meshes.
  // Prefix/suffix bytes are not part of the original model and must not
  // make its EXACT middle VB+IB window disappear.
  auto sharedVb=std::make_shared<xdk::OriginalGuestBufferBytes>(*vb);
  sharedVb->bytes.insert(sharedVb->bytes.begin(),24u,std::byte{0x7f});
  sharedVb->bytes.insert(sharedVb->bytes.end(),24u,std::byte{0x42});
  sharedVb->guest_size=uint32_t(sharedVb->bytes.size());
  auto sharedIb=std::make_shared<xdk::OriginalGuestBufferBytes>(*ib);
  sharedIb->bytes.insert(sharedIb->bytes.begin(),
                          {std::byte{0x66},std::byte{0x66}});
  sharedIb->bytes.insert(sharedIb->bytes.end(),
                          {std::byte{0x55},std::byte{0x55}});
  sharedIb->guest_size=uint32_t(sharedIb->bytes.size());
  auto packedDraw=realBufferDraw;
  packedDraw.source_vb0=sharedVb;
  packedDraw.source_ib=sharedIb;
  packedDraw.gpr[2]=1u; // baseVertex points to shared VB's second vertex
  packedDraw.gpr[3]=1u; // firstIndex points past first unrelated IB word
  const auto packed=native::IdentifyExactOriginalGuestMesh(packedDraw,models);
  assert(packed && packed->model_hash==0xCAFE &&
         packed->guest_first_vertex==1 &&
         packed->guest_first_index==1 &&
         packed->original_index_big_endian &&
         packed->original_index_element_bytes==2);
  // Reuse the SAME original source mesh uploaded as host index32 but
  // preserve byte-exact original Xbox D3DFMT_INDEX32 source allocation.
  auto ib32=std::make_shared<xdk::OriginalGuestBufferBytes>(*ib);
  ib32->bytes.clear();
  for(const uint32_t idx:realModel.decoded_meshes[0].triangle_list_indices)
    for(int b=3;b>=0;--b)
      ib32->bytes.push_back(std::byte((idx>>(8*b))&255u));
  ib32->guest_size=uint32_t(ib32->bytes.size());
  auto draw32=realBufferDraw;
  draw32.source_ib=ib32;
  const auto match32=native::IdentifyExactOriginalGuestMesh(draw32,models);
  assert(match32 && match32->original_index_big_endian &&
         match32->original_index_element_bytes==4);
  // Six D3DFMT_INDEX32 indices occupy twelve 16-bit words. A Draw
  // at element one requesting six indices must fail before k*4 access.
  // The historical /2 bounds check incorrectly admitted that range.
  auto beyond32=draw32;
  beyond32.gpr[3]=1u;
  assert(!native::IdentifyExactOriginalGuestMesh(beyond32,models));
  auto shared32=std::make_shared<xdk::OriginalGuestBufferBytes>(*ib32);
  shared32->bytes.insert(shared32->bytes.begin(),
      {std::byte{0},std::byte{0},std::byte{0x01},std::byte{0xFE}});
  shared32->guest_size=uint32_t(shared32->bytes.size());
  draw32.source_ib=shared32;
  draw32.gpr[3]=1;
  const auto packed32=native::IdentifyExactOriginalGuestMesh(draw32,models);
  assert(packed32 && packed32->guest_first_index==1 &&
         packed32->original_index_element_bytes==4);
  draw32.gpr[3]=0;
  assert(!native::IdentifyExactOriginalGuestMesh(draw32,models));
  auto wrongSharedFirst=packedDraw;
  wrongSharedFirst.gpr[3]=0; // points outside proven mesh's IB window
  assert(!native::IdentifyExactOriginalGuestMesh(wrongSharedFirst,models));
  auto wrongSharedBase=packedDraw;
  wrongSharedBase.gpr[2]=0; // index 0 now points to preceding mesh vertex
  assert(!native::IdentifyExactOriginalGuestMesh(wrongSharedBase,models));
  auto duplicateShared=std::make_shared<xdk::OriginalGuestBufferBytes>(*sharedVb);
  duplicateShared->bytes.insert(
      duplicateShared->bytes.end(),vb->bytes.begin(),vb->bytes.end());
  duplicateShared->guest_size=uint32_t(duplicateShared->bytes.size());
  auto duplicateCase=packedDraw;
  duplicateCase.source_vb0=duplicateShared;
  // The SECOND identical source stream does not satisfy this Draw's
  // exact baseVertex; the original first window remains unambiguous.
  const auto uniqueActual=native::IdentifyExactOriginalGuestMesh(
      duplicateCase,models);
  assert(uniqueActual && uniqueActual->guest_first_vertex==1);
  auto wrongIb=std::make_shared<xdk::OriginalGuestBufferBytes>(*ib);
  wrongIb->bytes[0]=std::byte{0xFA};
  realBufferDraw.source_ib=wrongIb;
  assert(!native::IdentifyExactOriginalGuestMesh(realBufferDraw,models));
  realBufferDraw.source_ib=ib;
  auto outOfBounds=realBufferDraw;
  outOfBounds.gpr[3]=120u;
  assert(!native::IdentifyExactOriginalGuestMesh(outOfBounds,models));
  outOfBounds=realBufferDraw;
  outOfBounds.gpr[2]=0xFFFFFFFFu;
  assert(!native::IdentifyExactOriginalGuestMesh(outOfBounds,models));
  std::vector<const model::UploadedOriginalModel*> ambiguousResidents{&resident,&resident};
  assert(!native::IdentifyExactOriginalGuestMesh(realBufferDraw,ambiguousResidents));
  auto wrongVb=std::make_shared<xdk::OriginalGuestBufferBytes>(*vb);
  wrongVb->guest_object^=4u;
  realBufferDraw.source_vb0=wrongVb;
  assert(!native::IdentifyExactOriginalGuestMesh(realBufferDraw,models));
  // Restore the real source buffer before testing actual EoR witness
  // disambiguation; the negative ownership check above must not poison
  // the following positive test with deliberately forged object identity.
  realBufferDraw.source_vb0=vb;
  // Two otherwise valid original EoR material witnesses can share XDK
  // guest VS/PS/texture objects. The only real mesh is the one proven by
  // the CURRENT bound VB+IB bytes and guest index window.
  auto otherModel=MakeModel();
  otherModel.model.original_resource_hash=0xBABEu;
  otherModel.decoded_meshes[0].original_vertex_stream[0]=std::byte{0x7f};
  Fake otherGpu;
  auto otherResident=model::UploadOriginalModel(std::move(otherModel),otherGpu);
  std::vector<const model::UploadedOriginalModel*> twoResidences{
      &resident,&otherResident};
  auto trueOwner=witness;
  auto* realSubmission=std::get_if<native::VerifiedXdkIndexedSubmission>(
      &trueOwner.original_source_submission);
  assert(realSubmission);
  realSubmission->original_guest_call.base_vertex=0;
  realSubmission->original_guest_call.first_index=0;
  auto falseOwner=trueOwner;
  auto* wrongSubmission=std::get_if<native::VerifiedXdkIndexedSubmission>(
      &falseOwner.original_source_submission);
  assert(wrongSubmission);
  wrongSubmission->native_draw.original_model_hash=0xBABEu;
  wrongSubmission->native_draw.original_sampler_slots[0].original_model_hash=
      0xBABEu;
  const std::vector<native::VerifiedXdkResourceWitness> choices{
      trueOwner,falseOwner};
  auto sourceFrame=runtime;
  sourceFrame.calls[0]=realBufferDraw;
  const auto recoveredOwner=native::ResolveCapturedResourceDraws(
      sourceFrame,choices,twoResidences);
  assert(recoveredOwner.size()==1 &&
         recoveredOwner[0].original_model_hash==0xCAFE &&
         recoveredOwner[0].original_mesh_header_offset==40);
  // Remove only the original actual buffer evidence. Without it, neither
  // of two candidate engine owners can claim exclusive source identity.
  sourceFrame.calls[0].source_vb0.reset();
  sourceFrame.calls[0].source_ib.reset();
  Reject([&]{(void)native::ResolveCapturedResourceDraws(
      sourceFrame,choices,twoResidences);});
  // Keep authentic bytes but change only the original index buffer
  // contents: no false owner can rescue the rejected verified mesh.
  sourceFrame.calls[0]=realBufferDraw;
  auto mutated=std::make_shared<xdk::OriginalGuestBufferBytes>(*ib);
  mutated->bytes[2]=std::byte{0xFA};
  sourceFrame.calls[0].source_ib=mutated;
  Reject([&]{(void)native::ResolveCapturedResourceDraws(
      sourceFrame,choices,twoResidences);});
  // A single plausible EoR shader/material witness must not hide a
  // contradiction in the captured original vertex/index bytes.
  const std::vector<native::VerifiedXdkResourceWitness> oneChoice{trueOwner};
  Reject([&]{(void)native::ResolveCapturedResourceDraws(
      sourceFrame,oneChoice,twoResidences);});
  sourceFrame.calls[0]=realBufferDraw;
  sourceFrame.calls[0].source_vb0=wrongVb;
  Reject([&]{(void)native::ResolveCapturedResourceDraws(
      sourceFrame,oneChoice,twoResidences);});
  const auto recovered=native::ResolveCapturedResourceDraws(runtime,catalog,models);
  native::ValidateLiveOriginalShaderPasses(runtime,recovered,effects);
  const auto twoEffects=Catalog(true);
  auto shaderFrame=runtime;
  shaderFrame.calls.push_back(runtime.calls[0]);
  auto shaderMatches=recovered;
  auto secondShaderMatch=shaderMatches[0];
  secondShaderMatch.original_call_ordinal=1;
  shaderMatches.push_back(secondShaderMatch);
  native::ValidateLiveOriginalShaderPasses(shaderFrame,shaderMatches,twoEffects);
  shaderMatches[1].original_effect_hash=0x4567;
  shaderMatches[1].original_pass_index=0;
  shaderMatches[1].vertex_source_sha256.fill(0x33);
  shaderMatches[1].pixel_source_sha256.fill(0x44);
  // Same live XDK shader object with two different original digests is
  // not a source-verified engine material.
  Reject([&]{native::ValidateLiveOriginalShaderPasses(
      shaderFrame,shaderMatches,twoEffects);});
  auto distinctShaderObjects=*shaderFrame.calls[1].observed_vs_matrices;
  distinctShaderObjects.original_vs_guest_object^=4u;
  distinctShaderObjects.original_ps_guest_object^=4u;
  shaderFrame.calls[1].observed_vs_matrices=distinctShaderObjects;
  native::ValidateLiveOriginalShaderPasses(shaderFrame,shaderMatches,twoEffects);
  assert(recovered.size()==1 && recovered[0].original_model_hash==0xCAFE &&
         recovered[0].original_vs_matrices &&
         xdk::ReadOriginalMatrix(*recovered[0].original_vs_matrices,11,4)
             ->values[0]==2.0f);

  // The real captured entrypoints MUST forward the whole source chronology.
  // Previously both routes called ReplayVerifiedGeometryScene, which dropped
  // the XDK events and made ReplayAotScene reject every tagged live Draw.
  auto exactTimeline=runtime;
  exactTimeline.sequence=17;
  xdk::RawCommand sourceRt{xdk::CommandKind::SetRenderTarget,
      0x8287F2C0u,0x8263A000u,{0x40002880u,0,0,0,0,0,0,0}};
  xdk::RawCommand sourceDepth{xdk::CommandKind::SetDepthStencilSurface,
      0x8287F628u,0x8263A004u,{0x40002880u,0,0,0,0,0,0,0}};
  xdk::RawCommand sourceClear{xdk::CommandKind::ClearF,
      0x82888C40u,0x8263A008u,{0x40002880u,1u,0,0,0,0,0,0}};
  sourceClear.captured_clear=xdk::OriginalXdkClearF{};
  sourceClear.captured_clear->flags=1;
  sourceClear.captured_clear->has_color=true;
  xdk::RawCommand sourceDraw{xdk::CommandKind::DrawIndexedVertices,
      exactTimeline.calls[0].function,exactTimeline.calls[0].lr,
      exactTimeline.calls[0].gpr,0u};
  xdk::RawCommand sourceResolve{xdk::CommandKind::Resolve,
      0x828897F8u,0x8263A00Cu,{0x40002880u,0,0,0,0,0,0,0}};
  xdk::RawCommand sourcePresent{xdk::CommandKind::Present,
      0x82880778u,0x8263A010u,{0x40002880u,0,0,0,0,0,0,0}};
  exactTimeline.commands={
      sourceRt,sourceDepth,sourceClear,sourceDraw,sourceResolve,sourcePresent};
  assert(xdk::ValidateOriginalCommandTimeline(exactTimeline));
  const auto minimalShaders=MinimalPrograms();
  const auto minimalReflection=MinimalReflections();
  const auto nativeFrame=native::PlanFrame(
      {{1280,720},{1280,720},true,true,true,1});
  auto indexedProven=verified;
  TimelineGpu indexedGpu;
  native::ReplayCapturedXdkScene(
      exactTimeline,std::span<const native::VerifiedXdkIndexedSubmission>(
          &indexedProven,1),nativeFrame,models,minimalShaders,effects,
      minimalReflection,indexedGpu);
  const std::vector<xdk::CommandKind> sourceGpuSequence{
      xdk::CommandKind::SetRenderTarget,
      xdk::CommandKind::SetDepthStencilSurface,
      xdk::CommandKind::ClearF,
      xdk::CommandKind::Resolve,xdk::CommandKind::Present};
  assert(indexedGpu.events==sourceGpuSequence &&
         indexedGpu.begins==1 && indexedGpu.draws==1 &&
         indexedGpu.resolves==1 && indexedGpu.composites==1 &&
         indexedGpu.aborts==0);
  // The REAL hook publishes a completed guest Present frame to this
  // one-slot mailbox; the owner-verified host bridge consumes it once.
  xdk::CompletedFrameMailbox mailbox;
  mailbox.Publish(exactTimeline);
  auto delivered=mailbox.Take();
  assert(delivered && delivered->overwritten_since_take==0);
  TimelineGpu directGpu;
  bool delivered_scene_attempted=false;
  native::ReplayDeliveredResourceScene(
      *delivered,catalog,nativeFrame,models,minimalShaders,effects,
      minimalReflection,directGpu,&delivered_scene_attempted);
  assert(delivered_scene_attempted && directGpu.begins==1 && directGpu.draws==1 &&
         directGpu.sampler_binds==1 &&
         directGpu.resolves==1 && directGpu.composites==1 &&
         directGpu.events==sourceGpuSequence);
  // The original USA archive streamer may provide these models using
  // NativeWorldStream::ResidentShared(), which holds real GPU buffers through
  // the active frame. The owner collector must never infer models from a
  // common shader, wrong mesh offset or out-of-date XDK resource pointer.
  Fake residentGpu;
  auto streamResident=std::make_shared<const model::UploadedOriginalModel>(
      model::UploadOriginalModel(MakeModel(),residentGpu));
  auto sourceOwned=catalog;
  std::visit([&](auto& link){
      link.native_draw.original_frame_sequence=exactTimeline.sequence;
    },sourceOwned[0].original_source_submission);
  const auto lookup=[&](uint32_t hash)
      -> std::shared_ptr<const model::UploadedOriginalModel> {
    return hash==0xCAFE ? streamResident : nullptr;
  };
  auto ownedModels=native::RetainVerifiedOriginalFrameResidents(
      exactTimeline,sourceOwned,lookup);
  assert(ownedModels.size()==1 && ownedModels[0]==streamResident &&
         ownedModels[0]->meshes.meshes()[0].index_buffer!=0);
  const auto missingLookup=[](uint32_t)
      -> std::shared_ptr<const model::UploadedOriginalModel>{return nullptr;};
  Reject([&]{(void)native::RetainVerifiedOriginalFrameResidents(
      exactTimeline,sourceOwned,missingLookup);});
  auto changedOwner=sourceOwned;
  std::visit([](auto& link){
    link.native_draw.original_mesh_header_offset+=4;
  },changedOwner[0].original_source_submission);
  Reject([&]{(void)native::RetainVerifiedOriginalFrameResidents(
      exactTimeline,changedOwner,lookup);});
  changedOwner=sourceOwned;
  std::visit([](auto& link){
    link.native_draw.original_frame_sequence+=1;
  },changedOwner[0].original_source_submission);
  Reject([&]{(void)native::RetainVerifiedOriginalFrameResidents(
      exactTimeline,changedOwner,lookup);});
  changedOwner=sourceOwned;
  changedOwner[0].original_guest_resource_to_material_mapping_verified=false;
  Reject([&]{(void)native::RetainVerifiedOriginalFrameResidents(
      exactTimeline,changedOwner,lookup);});
  // A source-valid model remains resident while the caller holds it, even
  // when its temporary owner catalogue releases its own reference.
  streamResident.reset();
  assert(ownedModels[0] && ownedModels[0]->original_resource_hash==0xCAFE);
  // A destroyed/late original frame is NOT eligible for host Present.
  mailbox.Publish(exactTimeline);
  auto nextOriginal=exactTimeline;
  nextOriginal.sequence=18;
  mailbox.Publish(nextOriginal);
  delivered=mailbox.Take();
  assert(delivered && delivered->overwritten_since_take==1);
  TimelineGpu missedGpu;
  bool missed_scene_attempted=true;
  Reject([&]{native::ReplayDeliveredResourceScene(
      *delivered,catalog,nativeFrame,models,minimalShaders,effects,
      minimalReflection,missedGpu,&missed_scene_attempted);});
  assert(!missed_scene_attempted && missedGpu.begins==0 && missedGpu.draws==0 &&
         missedGpu.composites==0);
  std::vector<native::VerifiedXdkDrawSubmission> oneMixed{indexedProven};
  TimelineGpu mixedGpu;
  native::ReplayCapturedMixedScene(
      exactTimeline,oneMixed,nativeFrame,models,minimalShaders,effects,
      minimalReflection,mixedGpu);
  assert(mixedGpu.events==sourceGpuSequence &&
         mixedGpu.begins==1 && mixedGpu.draws==1 &&
         mixedGpu.resolves==1 && mixedGpu.composites==1 &&
         mixedGpu.aborts==0);
  auto missingTimeline=exactTimeline;
  missingTimeline.commands.clear();
  TimelineGpu rejectedTimeline;
  Reject([&]{native::ReplayCapturedXdkScene(
      missingTimeline,std::span<const native::VerifiedXdkIndexedSubmission>(
          &indexedProven,1),nativeFrame,models,minimalShaders,effects,
      minimalReflection,rejectedTimeline);});
  assert(rejectedTimeline.begins==0 && rejectedTimeline.events.empty());

  // Identical resident meshes and materials may be replayed with a new
  // per-instance matrix in the next frame without hardcoding their order.
  auto transformed=runtime;
  transformed.sequence=3;
  transformed.calls[0].observed_vs_matrices->c7_through_c25[(11-7)*4]=3.0f;
  const auto second=native::ResolveCapturedResourceDraws(transformed,catalog,models);
  assert(second.size()==1 && second[0].original_frame_sequence==3 &&
         xdk::ReadOriginalMatrix(*second[0].original_vs_matrices,11,4)
             ->values[0]==3.0f);
  // Same source model, shader and resident guest IB, but later XDK Draw
  // addresses a different triangle in the *verified complete* index stream.
  // No hardcoded per-frame Draw table is needed once the source stream
  // identity and linear guest->host mapping are independently proven.
  auto later=runtime;
  later.sequence=4;
  later.calls[0].gpr[3]=123u;  // firstIndex+3 -> host mesh index 3
  Reject([&]{(void)native::ResolveCapturedResourceDraws(later,catalog,models);});
  catalog[0].original_linear_index_stream_verified=true;
  const auto second_triangle=native::ResolveCapturedResourceDraws(
      later,catalog,models);
  assert(second_triangle.size()==1 && second_triangle[0].first_index==3 &&
         second_triangle[0].host_base_vertex==0 &&
         second_triangle[0].original_call_ordinal==0);
  const auto uploaded=native::BindVerifiedGeometryDraws(
      second_triangle,models,effects);
  assert(uploaded.size()==1 && uploaded[0].draw.first_index==3 &&
         uploaded[0].original_mesh_header_offset==40);
  // A verified shared index stream can also contain a triangle beginning
  // at an unaligned firstIndex: keep the source count and bounds exact.
  later.calls[0].gpr[3]=121u;
  const auto offset_triangle=native::ResolveCapturedResourceDraws(
      later,catalog,models);
  assert(offset_triangle.size()==1 && offset_triangle[0].first_index==1);
  const auto offset_gpu=native::BindVerifiedGeometryDraws(
      offset_triangle,models,effects);
  assert(offset_gpu.size()==1 && offset_gpu[0].draw.first_index==1);
  later.calls[0].gpr[3]=123u;
  // Guest baseVertex -5 is translated from the authenticated guest -2
  // origin to native -3. This local rebase is in bounds for indices 3..5.
  later.calls[0].gpr[2]=0xFFFFFFFBu;
  const auto rebased=native::ResolveCapturedResourceDraws(later,catalog,models);
  assert(rebased.size()==1 && rebased[0].host_base_vertex==-3);
  const auto rebased_gpu=native::BindVerifiedGeometryDraws(
      rebased,models,effects);
  assert(rebased_gpu.size()==1 && rebased_gpu[0].draw.base_vertex==-3);
  // An authenticated model does NOT make an out-of-range dynamic index
  // window valid; only the exact resident mesh's decoded range can pass.
  later.calls[0].gpr[3]=126u;
  Reject([&]{const auto bad=native::ResolveCapturedResourceDraws(
      later,catalog,models);
      (void)native::BindVerifiedGeometryDraws(bad,models,effects);});
  catalog[0].original_linear_index_stream_verified=false;
  // A null original guest IB must never establish source ownership merely
  // because both witness and captured state happen to contain zero.
  auto missingIb=runtime;
  missingIb.calls[0].observed_vs_matrices->
      original_index_buffer_guest_object=0;
  auto invalidIbWitness=catalog;
  invalidIbWitness[0].original_resources.index_buffer=0;
  Reject([&]{(void)native::ResolveCapturedResourceDraws(
      missingIb,invalidIbWitness,models);});
  // Exact original USA VB stream 0 differentiates meshes that share shaders.
  // Stale stream 15 is ignored when source declaration consumes only slot 0.
  transformed.calls[0].observed_vs_matrices->
      original_vertex_buffer_guest_objects[15]=0x8300FEEDu;
  assert(native::ResolveCapturedResourceDraws(transformed,catalog,models).size()==1);
  transformed.calls[0].observed_vs_matrices->
      original_vertex_buffer_guest_objects[0]=0x8300BAD0u;
  Reject([&]{(void)native::ResolveCapturedResourceDraws(transformed,catalog,models);});
  transformed=runtime;
  catalog[0].source_verified_vertex_stream_slot_mask=0;
  Reject([&]{(void)native::ResolveCapturedResourceDraws(transformed,catalog,models);});
  catalog[0]=witness;
  catalog[0].original_xdk_vertex_stream_slot_mapping_verified=false;
  Reject([&]{(void)native::ResolveCapturedResourceDraws(transformed,catalog,models);});
  catalog[0]=witness;
  // Stale XDK slot 15 is irrelevant when original shader reads only slot 0.
  transformed.calls[0].observed_vs_matrices->
      original_texture_guest_objects[15]=0x8300ABCDu;
  const auto untouched=native::ResolveCapturedResourceDraws(transformed,catalog,models);
  assert(untouched.size()==1);
  transformed.calls[0].observed_vs_matrices->
      original_texture_guest_objects[0]=0x8300BAD0u;
  Reject([&]{(void)native::ResolveCapturedResourceDraws(transformed,catalog,models);});
  transformed=runtime;
  transformed.calls[0].observed_vs_matrices.reset();
  Reject([&]{(void)native::ResolveCapturedResourceDraws(transformed,catalog,models);});
  transformed=runtime;
  catalog.push_back(witness);
  Reject([&]{(void)native::ResolveCapturedResourceDraws(transformed,catalog,models);});
  catalog.resize(1);
  catalog[0].original_guest_resource_to_material_mapping_verified=false;
  Reject([&]{(void)native::ResolveCapturedResourceDraws(transformed,catalog,models);});
  catalog[0]=witness;
  catalog[0].source_verified_texture_slot_mask=0;
  Reject([&]{(void)native::ResolveCapturedResourceDraws(transformed,catalog,models);});
  catalog[0]=witness;
  catalog[0].original_xdk_texture_slot_mapping_verified=false;
  Reject([&]{(void)native::ResolveCapturedResourceDraws(transformed,catalog,models);});
  assert(gpu.next==3);
  } catch(const std::exception& e) {
    std::fprintf(stderr,"[HULK/EOR-DRAW-EXCEPTION] %s\\n",e.what());
    return 1;
  } catch(...) {
    std::fprintf(stderr,"[HULK/EOR-DRAW-EXCEPTION] unknown\\n");
    return 1;
  }
}
