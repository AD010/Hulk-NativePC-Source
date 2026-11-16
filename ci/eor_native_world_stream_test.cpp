#include "../native_port/src/eor_native_world_stream.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace hulk::eor;
using namespace hulk::eor::world;
namespace {
std::atomic<int> calls{0};
class Source final : public ModelSource {
 public:
  std::optional<model::LoadedNativeGeometry> Load(uint32_t hash) override {
    ++calls;
    if(hash==33) return std::nullopt;
    model::LoadedNativeGeometry src;
    src.model.original_resource_hash=hash;
    src.model.source.platform=model::Platform::Xbox360;
    src.model.source.model_name="original_lod";
    src.model.source.surface_records=1;
    src.model.bytes.resize(96);
    model::SurfaceMaterials surf;
    surf.ordinal=0;
    surf.original_material_name="ground";
    surf.material_data_offset=18;
    src.original_surfaces.push_back(surf);
    model::NativeIndexedMesh mesh;
    mesh.source_header_offset=10;
    mesh.source_indices_offset=37;
    mesh.source_vertices_offset=43;
    mesh.original_vertex_stride=24;
    mesh.original_attribute_count=3;
    mesh.original_attribute_declarations.resize(3);
    mesh.positions={{0,0,0},{1,0,0},{0,1,0}};
    mesh.triangle_list_indices={0,1,2};
    mesh.original_vertex_stream.resize(72);
    src.decoded_meshes.push_back(std::move(mesh));
    return src;
  }
};
struct FakeGpu final : model::NativeMeshUploadBackend {
  uint64_t serial=0;
  std::vector<uint64_t> freed;
  uint64_t UploadPositions(std::span<const model::NativePosition> v) override {assert(v.size()==3);return ++serial;}
  uint64_t UploadOriginalVertexStream(std::span<const std::byte> v) override {assert(v.size()==72);return ++serial;}
  uint64_t UploadIndices(std::span<const uint32_t> v) override {assert(v.size()==3);return ++serial;}
  void ReleaseBuffer(uint64_t h) noexcept override {freed.push_back(h);}
};
template<class F> void RetryUntil(F f) {
  for(int i=0;i<3000;++i) {
    if(f())return;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  throw std::runtime_error("EoR source load timed out");
}
}
int main() {
  FakeGpu gpu;
  {
    NativeWorldStream streamer(std::make_unique<Source>(),gpu,
        {{11,100.0},{22,300.0},{33,std::numeric_limits<double>::infinity()}});
    assert(streamer.Tick(20).request_model_hash==11);
    RetryUntil([&]{streamer.Tick(20);return streamer.Resident(11)!=nullptr;});
    assert(streamer.Resident(11)->meshes.meshes().size()==1);
    assert(streamer.Tick(80).prefetch_model_hash==22);
    RetryUntil([&]{streamer.Tick(80);return streamer.Resident(22)!=nullptr;});
    assert(streamer.Tick(120).active_model_hash==22);
    assert(streamer.Tick(999).request_model_hash==33);
    RetryUntil([&]{streamer.Tick(999);return streamer.Failed(33);});
    assert(streamer.Tick(999).active_model_hash==22);
    const auto before=calls.load();
    for(int n=0;n<1000;++n)streamer.Tick(999);
    assert(calls.load()==before); // corrupted/unavailable original model never retried every frame
    streamer.RetryFailed(33);
    RetryUntil([&]{streamer.Tick(999);return calls.load()>before;});
    assert(streamer.Resident(22) && streamer.Resident(11));
    bool refused=false;
    try {streamer.Tick(std::numeric_limits<double>::quiet_NaN());} catch(const std::invalid_argument&){refused=true;}
    assert(refused);
  }
  assert(gpu.serial==6);
  assert(gpu.freed.size()==6);
}
