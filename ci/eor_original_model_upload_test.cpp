#include "../native_port/src/eor_original_model_upload.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>
#include <cstdio>
#include <cstdlib>

#undef assert
#define assert(check) do { if (!(check)) { \
  std::fprintf(stderr,"[HULK/TEST] %s:%d: %s\n",__FILE__,__LINE__,#check); \
  std::fflush(stderr); std::abort(); } } while(false)
using namespace hulk::eor::model;
namespace {
struct Fake final : NativeMeshUploadBackend {
 unsigned allocated=0;std::vector<uint64_t> freed;
 uint64_t UploadDecodedAttributes(std::span<const HostAttribute> v)override{
   assert(v.size()==3);return ++allocated;
 }
 uint64_t UploadPositions(std::span<const NativePosition> v)override{assert(v.size()==3);return ++allocated;}
 uint64_t UploadOriginalVertexStream(std::span<const std::byte> v)override{assert(v.size()==72);return ++allocated;}
 uint64_t UploadIndices(std::span<const uint32_t> v)override{assert(v.size()==3);return ++allocated;}
 void ReleaseBuffer(uint64_t h) noexcept override {freed.push_back(h);}
};
LoadedNativeGeometry Model() {
 LoadedNativeGeometry src;
 src.model.original_resource_hash=0x12345678;
 src.model.source.platform=Platform::Xbox360;
 src.model.source.model_name="test_model";
 src.model.source.surface_records=1;
 src.model.bytes.resize(128);
 SurfaceMaterials surf;
 surf.ordinal=0;surf.original_material_name="blinn1";surf.material_flags=7;surf.material_data_offset=28;
 TextureReferenceCandidate tex;
 tex.original_texture_hash=0x98765432;tex.offset_from_surface_data=56;
 tex.original_resource.hash=tex.original_texture_hash;tex.original_resource.size=128;
 surf.indexed_texture_references.push_back(tex);
 src.original_surfaces.push_back(surf);
 NativeIndexedMesh mesh;
 mesh.source_header_offset=10;mesh.source_indices_offset=33;mesh.source_vertices_offset=39;
 mesh.original_vertex_stride=24;mesh.original_attribute_count=2;
 mesh.original_attribute_declarations={
        {std::byte{2},std::byte{0},std::byte{0},std::byte{12}},
        {std::byte{0},std::byte{5},std::byte{0},std::byte{0}}};
 mesh.positions={{0,0,0},{1,0,0},{0,1,0}};
 mesh.triangle_list_indices={0,1,2};
 mesh.original_vertex_stream.resize(72);
 src.decoded_meshes.push_back(std::move(mesh));
 return src;
}
template<class F> void Rejected(F f){bool fail=false;try{f();}catch(const std::invalid_argument&){fail=true;}assert(fail);}
}
int main(){
 Fake backend;
 {
  auto result=UploadOriginalModel(Model(),backend);
  assert(result.original_resource_hash==0x12345678);
  assert(result.original_identity.model_name=="test_model");
  assert(result.original_surface_candidates.size()==1);
  assert(result.original_surface_candidates[0].indexed_texture_references[0].original_texture_hash==0x98765432);
  assert(result.meshes.meshes().size()==1);
  assert(result.meshes.meshes()[0].original_header_offset==10);
 }
 assert((backend.freed==std::vector<uint64_t>{3,2,1}));
 Fake with_uv;
 {
  auto source=Model();
   auto& mesh=source.decoded_meshes[0];
   mesh.original_attribute_count=3;
   mesh.original_attribute_declarations.insert(mesh.original_attribute_declarations.begin()+1,
       std::array<std::byte,4>{std::byte{0x2e},std::byte{5},std::byte{0},std::byte{20}});
   mesh.original_attribute_declarations.back()[2]=std::byte{1};
   mesh.source_indices_offset=37;mesh.source_vertices_offset=43;
   auto a=UploadOriginalModel(std::move(source),with_uv);
  assert(a.meshes.meshes()[0].decoded_attribute_buffers[0]==4);
 }
 assert((with_uv.freed==std::vector<uint64_t>{4,3,2,1}));
 Fake invalid;
 auto bad=Model();bad.model.source.platform=Platform::PcD3d9;
 Rejected([&]{(void)UploadOriginalModel(std::move(bad),invalid);});
 bad=Model();bad.original_surfaces[0].ordinal=3;
 Rejected([&]{(void)UploadOriginalModel(std::move(bad),invalid);});
 bad=Model();bad.original_surfaces[0].indexed_texture_references[0].original_resource.hash=6;
 Rejected([&]{(void)UploadOriginalModel(std::move(bad),invalid);});
 bad=Model();bad.model.source.surface_records=2;
 Rejected([&]{(void)UploadOriginalModel(std::move(bad),invalid);});
 bad=Model();bad.decoded_meshes[0].original_attribute_declarations[0]={};
  Rejected([&]{(void)UploadOriginalModel(std::move(bad),invalid);});
  assert(invalid.allocated==0 && invalid.freed.empty());
}
