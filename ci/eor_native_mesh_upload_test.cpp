#include "../native_port/src/eor_native_mesh_upload.h"
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
 uint64_t next=1,repeat=0;
 unsigned allocations=0,fail_at=0;
 std::vector<uint64_t> freed;
 std::vector<NativePosition> positions;
 std::vector<std::byte> raw;
 std::vector<uint32_t> indices;
 uint64_t Allocate(){if(++allocations==fail_at)return 0;return repeat?repeat:next++;}
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
 MustThrow([&]{(void)UploadNativeIndexedMeshes({},preflight);});
}
