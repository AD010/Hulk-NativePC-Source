#include "../native_port/src/eor_native_mesh_upload.h"
#include "../native_port/src/eor_host_vertex_layout.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace hulk::eor::model;
namespace {
struct Fake final : NativeMeshUploadBackend {
 uint64_t next=1,repeat=0,alias_at=0,alias_handle=0;
 unsigned allocations=0,fail_at=0;
 std::vector<uint64_t> freed;
 std::vector<NativePosition> positions;
 std::vector<std::byte> raw;
 std::vector<uint32_t> indices;
 std::vector<HostAttribute> attributes;
 uint64_t UploadDecodedAttributes(std::span<const HostAttribute> v) override {
   attributes.assign(v.begin(),v.end());return Allocate();
 }
 uint64_t Allocate(){
   ++allocations;
   if(allocations==fail_at)return 0;
   if(allocations==alias_at)return alias_handle;
   return repeat?repeat:next++;
 }
 uint64_t UploadPositions(std::span<const NativePosition> p) override {positions.assign(p.begin(),p.end());return Allocate();}
 uint64_t UploadOriginalVertexStream(std::span<const std::byte> p) override {raw.assign(p.begin(),p.end());return Allocate();}
 uint64_t UploadIndices(std::span<const uint32_t> p) override {indices.assign(p.begin(),p.end());return Allocate();}
 void ReleaseBuffer(uint64_t h) noexcept override {freed.push_back(h);}
};
NativeIndexedMesh Mesh(size_t n) {
 NativeIndexedMesh m;
 m.source_header_offset=n;m.source_indices_offset=n+27;m.source_vertices_offset=n+33;
 m.original_vertex_stride=24;m.original_attribute_count=3;
 m.original_attribute_declarations.resize(3);
 m.original_attribute_declarations[0][0]=std::byte(0xab);
 m.positions={{-1.f,0,0},{1.f,0,0},{0,1.f,0}};
 m.triangle_list_indices={0,1,2};
 m.original_vertex_stream.assign(72,std::byte(0x7f));
 return m;
}
template<typename F> void MustThrow(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}assert(caught);}
}
int main(){
 using hulk::eor::gpu::HostVertexAttribute;
 using hulk::eor::gpu::ValidateHostVertexLayout;
 constexpr uint32_t automatic = UINT32_MAX;
 const auto verified_layout=[&](std::initializer_list<HostVertexAttribute> source){
   return ValidateHostVertexLayout(
       std::span<const HostVertexAttribute>(source.begin(),source.size()),
       automatic,automatic);
 };
 const HostVertexAttribute xyz{0,3,0,12,true,true};
 const HostVertexAttribute tex0{1,2,0,16,true,true};
 const HostVertexAttribute tex1{1,2,8,16,true,true};
 assert(verified_layout({xyz}));
 assert(verified_layout({xyz,tex0,tex1}));
 assert(verified_layout({xyz,tex0,{1,2,automatic,automatic,true,true}}));
 assert(verified_layout({xyz,{1,4,0,automatic,true,true}}));
 assert(!verified_layout({}));
 assert(!verified_layout({tex0}));
 assert(!verified_layout({xyz,xyz}));
 assert(!verified_layout({xyz,{0,2,0,12,true,true}}));
 assert(!verified_layout({xyz,{1,2,0,automatic,true,true}}));
 assert(verified_layout({xyz,{2,4,0,16,true,true}})); // pass omits resident slot 1
 // AUTO_OFFSET follows the largest prior end, including out-of-order declarations.
 assert(verified_layout({xyz,{1,1,8,16,true,true},{1,1,0,16,true,true},
                         {1,1,automatic,16,true,true}}));
 assert(!verified_layout({xyz,{1,1,12,16,true,true},{1,1,0,16,true,true},
                          {1,1,automatic,16,true,true}}));
 // One explicit stride supplies every attribute in a partially consumed float4.
 assert(verified_layout({xyz,{1,1,0,automatic,true,true},{1,1,4,16,true,true}}));
 assert(!verified_layout({xyz,tex0,{1,2,4,16,true,true}}));
 assert(!verified_layout({xyz,tex0,{1,2,12,16,true,true}}));
 assert(!verified_layout({xyz,{1,4,4,16,true,true}}));
 assert(!verified_layout({xyz,{1,4,0,12,true,true}}));
 assert(!verified_layout({xyz,{1,4,0,16,false,true}}));
 assert(!verified_layout({xyz,{1,4,0,16,true,false}}));
 assert(!verified_layout({xyz,{16,4,0,16,true,true}}));
 assert(!verified_layout({xyz,{1,0,0,16,true,true}}));
 assert(!verified_layout({xyz,{1,5,0,16,true,true}}));
 assert(!verified_layout({xyz,{1,1,3,16,true,true}}));
 const std::vector<NativeIndexedMesh> original{Mesh(120),Mesh(300)};
 Fake success;
 {
   auto a=UploadNativeIndexedMeshes(original,success);
   assert(a.meshes().size()==2);
   assert(a.meshes()[0].position_buffer==1&&a.meshes()[0].original_vertex_stream_buffer==2&&a.meshes()[0].index_buffer==3);
   assert(a.meshes()[1].index_buffer==6);
   assert(a.meshes()[0].original_vertex_stride==24);
   assert((a.meshes()[0].host_indices==std::vector<uint32_t>{0,1,2}));
   assert(a.meshes()[0].min_index==0 && a.meshes()[0].max_index==2);
   assert(a.meshes()[0].original_attribute_declarations[0][0]==std::byte(0xab));
   assert(success.raw.size()==72&&success.raw[0]==std::byte(0x7f));
   assert(success.positions[0].x==-1.f&&success.indices[2]==2);
   auto moved=std::move(a);
   assert(a.meshes().empty()&&moved.meshes().size()==2);
 }
 assert((success.freed==std::vector<uint64_t>{6,5,4,3,2,1}));
 // Verified Big Endian UVs actually become owned host float4 Vulkan slots.
 std::vector<std::vector<VerifiedAttributeLayout>> slots{
   {{12,2,AttributeEncoding::BigUNorm16}},
   {{12,2,AttributeEncoding::BigUNorm16}}};
 Fake with_attributes;
 {
   auto a=UploadNativeIndexedMeshes(original,with_attributes,slots);
   assert(a.meshes()[0].decoded_attribute_buffers.size()==1);
   assert(a.meshes()[0].decoded_attribute_buffers[0]==4);
   assert(a.meshes()[1].decoded_attribute_buffers[0]==8);
   assert(with_attributes.attributes.size()==3);
   assert(with_attributes.attributes[0][0]==float(0x7f7f)/65535.f);
 }
 assert((with_attributes.freed==std::vector<uint64_t>{8,7,6,5,4,3,2,1}));
 Fake allocation_failure;allocation_failure.fail_at=8;
 MustThrow([&]{(void)UploadNativeIndexedMeshes(original,allocation_failure,slots);});
 assert((allocation_failure.freed==std::vector<uint64_t>{7,6,5,4,3,2,1}));
 Fake bad_layout;
 auto missing=slots;missing.pop_back();
 MustThrow([&]{(void)UploadNativeIndexedMeshes(original,bad_layout,missing);});
 auto overflow=slots;overflow[1][0].original_byte_offset=23;
 MustThrow([&]{(void)UploadNativeIndexedMeshes(original,bad_layout,overflow);});
 auto duplicate=slots;duplicate[1].push_back(duplicate[1][0]);
 MustThrow([&]{(void)UploadNativeIndexedMeshes(original,bad_layout,duplicate);});
 assert(bad_layout.allocations==0);
 for(unsigned fail=1;fail<=6;++fail){
  Fake gpu;gpu.fail_at=fail;
  MustThrow([&]{(void)UploadNativeIndexedMeshes(original,gpu);});
  assert(gpu.freed.size()==fail-1);
  for(size_t j=0;j<gpu.freed.size();++j) assert(gpu.freed[j]==fail-j-1);
 }
 Fake preflight;auto bad=original;
 bad[1].triangle_list_indices[2]=999;
 MustThrow([&]{(void)UploadNativeIndexedMeshes(bad,preflight);});
 bad=original;bad[1].positions[0].x=std::numeric_limits<float>::quiet_NaN();
 MustThrow([&]{(void)UploadNativeIndexedMeshes(bad,preflight);});
 bad=original;bad[1].original_vertex_stream.pop_back();
 MustThrow([&]{(void)UploadNativeIndexedMeshes(bad,preflight);});
 bad=original;bad[1].source_indices_offset+=1;
 MustThrow([&]{(void)UploadNativeIndexedMeshes(bad,preflight);});
 assert(preflight.allocations==0 && preflight.freed.empty());
 Fake alias;alias.repeat=7;
 MustThrow([&]{(void)UploadNativeIndexedMeshes(original,alias);});
 assert((alias.freed==std::vector<uint64_t>{7}));
 // The last UV upload tries to reuse model 0's decoded attribute handle.
 // Only the first model's attribute owner may release handle 4; the
 // rejected alias must never become a second owner or be freed twice.
 Fake attribute_alias;
 attribute_alias.alias_at=8;attribute_alias.alias_handle=4;
 MustThrow([&]{(void)UploadNativeIndexedMeshes(original,attribute_alias,slots);});
 assert(attribute_alias.allocations==8);
 assert((attribute_alias.freed==std::vector<uint64_t>{7,6,5,4,3,2,1}));
 MustThrow([&]{(void)UploadNativeIndexedMeshes({},preflight);});
}
