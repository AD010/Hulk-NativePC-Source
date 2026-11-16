#include "eor_diligent_mesh_buffers.h"
#include "RenderDevice.h"
#include "Buffer.h"
#include "RefCntAutoPtr.hpp"
#include <limits>
#include <cmath>
#include <map>
#include <stdexcept>
#include <type_traits>

namespace hulk::eor::gpu {
namespace {
constexpr uint64_t kMaxUploadBytes = 256ull << 20;
static_assert(sizeof(model::NativePosition) == 3 * sizeof(float));
static_assert(std::is_trivially_copyable_v<model::NativePosition>);
}
struct DiligentMeshUploadBackend::State {
  explicit State(Diligent::IRenderDevice* dev) : device(dev) {}
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
  struct Entry {
    Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
    uint32_t kind=UINT32_MAX;
  };
  std::map<uint64_t,Entry> live;
  uint64_t next_handle = 1;
};
DiligentMeshUploadBackend::DiligentMeshUploadBackend(Diligent::IRenderDevice* device)
    : state_(device ? std::make_unique<State>(device) : nullptr) {
  if (!device) throw std::invalid_argument("Missing native EoR Vulkan/Diligent device");
}
DiligentMeshUploadBackend::~DiligentMeshUploadBackend() noexcept = default;
uint64_t DiligentMeshUploadBackend::Create(const void* bytes, uint64_t length, uint32_t kind) {
  if (!bytes || !length || length > kMaxUploadBytes || kind>3u ||
      state_->next_handle == std::numeric_limits<uint64_t>::max()) return 0;
  Diligent::BufferDesc description;
  description.Name = kind == 0 ? "EoR decoded XYZ" :
                     kind == 1 ? "EoR original BE attribute stream" :
                     kind == 2 ? "EoR native index32" :
                                 "EoR decoded native float4 attributes";
  description.Size = length;
  description.Usage = Diligent::USAGE_IMMUTABLE;
  // The original BE stream is not a verified host vertex layout. It is
  // uploaded and owned, but may not be submitted as a draw until decoded.
  description.BindFlags = kind == 2 ? Diligent::BIND_INDEX_BUFFER : Diligent::BIND_VERTEX_BUFFER;
  const Diligent::BufferData initial{bytes,length};
  try {
    Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
    state_->device->CreateBuffer(description,&initial,&buffer);
    if (!buffer) return 0;
    const uint64_t handle=state_->next_handle;
    state_->live.emplace(handle,State::Entry{std::move(buffer),kind});
    ++state_->next_handle;
    return handle;
  } catch (...) {
    // Caller's UploadedNativeMeshBatch rolls back all older buffer handles.
    return 0;
  }
}
uint64_t DiligentMeshUploadBackend::UploadPositions(std::span<const model::NativePosition> v) {
  if (v.size() > kMaxUploadBytes / sizeof(model::NativePosition)) return 0;
  return Create(v.data(),v.size_bytes(),0);
}
uint64_t DiligentMeshUploadBackend::UploadOriginalVertexStream(std::span<const std::byte> v) {
  return Create(v.data(),v.size_bytes(),1);
}
uint64_t DiligentMeshUploadBackend::UploadIndices(std::span<const uint32_t> v) {
  if (v.size() > kMaxUploadBytes / sizeof(uint32_t)) return 0;
  return Create(v.data(),v.size_bytes(),2);
}
uint64_t DiligentMeshUploadBackend::UploadDecodedAttributes(
    std::span<const model::HostAttribute> values) {
  if(values.empty() || values.size()>kMaxUploadBytes/sizeof(model::HostAttribute))
    return 0;
  for(const auto& vertex:values)
    for(float value:vertex)
      if(!std::isfinite(value))return 0;
  return Create(values.data(),values.size_bytes(),3);
}
void DiligentMeshUploadBackend::ReleaseBuffer(uint64_t handle) noexcept {
  state_->live.erase(handle);
}
Diligent::IBuffer* DiligentMeshUploadBackend::FindBuffer(uint64_t handle) const noexcept {
  const auto it=state_->live.find(handle);
  return it==state_->live.end()?nullptr:it->second.buffer.RawPtr();
}
uint32_t DiligentMeshUploadBackend::KindOf(uint64_t handle) const noexcept {
  const auto it=state_->live.find(handle);
  return it==state_->live.end()?UINT32_MAX:it->second.kind;
}
size_t DiligentMeshUploadBackend::LiveBufferCount() const noexcept {return state_->live.size();}
} // namespace hulk::eor::gpu
