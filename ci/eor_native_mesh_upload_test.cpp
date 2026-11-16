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
struct FakeUploader final : NativeMeshUploadBackend {
  uint64_t next = 1;
  unsigned allocations = 0, fail_at = 0;
  uint64_t repeat_handle = 0;
  std::vector<uint64_t> freed;
  std::vector<NativePosition> last_positions;
  std::vector<uint32_t> last_indices;
  uint64_t Allocate() {
    if (++allocations == fail_at) return 0;
    if (repeat_handle) return repeat_handle;
    return next++;
  }
  uint64_t UploadPositions(std::span<const NativePosition> data) override {
    last_positions.assign(data.begin(), data.end());return Allocate();
  }
  uint64_t UploadIndices(std::span<const uint32_t> data) override {
    last_indices.assign(data.begin(), data.end());return Allocate();
  }
  void ReleaseBuffer(uint64_t handle) noexcept override { freed.push_back(handle); }
};
NativeIndexedMesh Mesh(size_t offset) {
  NativeIndexedMesh m;
  m.source_header_offset=offset;
  m.source_indices_offset=offset+20;
  m.source_vertices_offset=offset+26;
  m.original_vertex_stride=24;
  m.original_attribute_count=3;
  m.original_attribute_declarations.resize(3);
  m.positions={{-1.f,0.f,0.f},{1.f,0.f,0.f},{0.f,1.f,0.f}};
  m.triangle_list_indices={0,1,2};
  m.original_vertex_stream.resize(72);
  return m;
}
template<class F> void MustThrow(F f) {
  bool caught = false;
  try { f(); } catch(const std::exception&) { caught = true; }
  assert(caught);
}
} // namespace
int main() {
  const std::vector<NativeIndexedMesh> original{Mesh(120),Mesh(300)};
  FakeUploader backend;
  {
    auto uploaded=UploadNativeIndexedMeshes(original,backend);
    assert(uploaded.meshes().size()==2);
    assert(uploaded.meshes()[0].original_header_offset==120);
    assert(uploaded.meshes()[0].position_buffer==1);
    assert(uploaded.meshes()[0].index_buffer==2);
    assert(uploaded.meshes()[1].index_buffer==4);
    assert(uploaded.meshes()[0].index_count==3);
    assert(backend.last_positions[0].x==-1.f);
    assert((backend.last_indices==std::vector<uint32_t>{0,1,2}));
    auto moved=std::move(uploaded);
    assert(uploaded.meshes().empty());
    assert(moved.meshes().size()==2);
  }
  assert((backend.freed==std::vector<uint64_t>{4,3,2,1}));
  FakeUploader late_bad;
  auto corrupt=original;
  corrupt[1].triangle_list_indices[2]=999;
  MustThrow([&]{ (void)UploadNativeIndexedMeshes(corrupt,late_bad); });
  assert(late_bad.allocations==0 && late_bad.freed.empty());
  corrupt=original;
  corrupt[1].positions[1].x=std::numeric_limits<float>::quiet_NaN();
  MustThrow([&]{ (void)UploadNativeIndexedMeshes(corrupt,late_bad); });
  assert(late_bad.allocations==0);
  corrupt=original;
  corrupt[1].original_vertex_stream.pop_back();
  MustThrow([&]{ (void)UploadNativeIndexedMeshes(corrupt,late_bad); });
  assert(late_bad.allocations==0);
  FakeUploader bad_second_index;bad_second_index.fail_at=4;
  MustThrow([&]{ (void)UploadNativeIndexedMeshes(original,bad_second_index); });
  assert((bad_second_index.freed==std::vector<uint64_t>{3,2,1}));
  FakeUploader bad_first_position;bad_first_position.fail_at=1;
  MustThrow([&]{ (void)UploadNativeIndexedMeshes(original,bad_first_position); });
  assert(bad_first_position.freed.empty());
  FakeUploader alias;alias.repeat_handle=7;
  MustThrow([&]{ (void)UploadNativeIndexedMeshes(original,alias); });
  assert((alias.freed==std::vector<uint64_t>{7}));
  MustThrow([&]{ (void)UploadNativeIndexedMeshes({},late_bad); });
}
