#include "eor_diligent_mesh_buffers.h"
#include "eor_immutable_buffer_cache.h"
#include "RenderDevice.h"
#include "Buffer.h"
#include "RefCntAutoPtr.hpp"
#include <limits>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace hulk::eor::gpu {
namespace {
constexpr uint64_t kMaxUploadBytes = 256ull << 20;
// Keep exact index-window checks cheap for repeated live EoR Vulkan draws.
// These original GPU index streams are immutable after uploading.
constexpr size_t kIndexBlockElements = 256;
static_assert(sizeof(model::NativePosition) == 3 * sizeof(float));
static_assert(std::is_trivially_copyable_v<model::NativePosition>);
}
struct DiligentMeshUploadBackend::State {
  explicit State(Diligent::IRenderDevice* dev) : device(dev) {}
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
  struct Entry {
    Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
    uint32_t kind=UINT32_MAX;
    uint32_t maximum_index=UINT32_MAX;
    uint32_t minimum_index=UINT32_MAX;
    std::vector<uint32_t> immutable_indices;
    std::vector<std::pair<uint32_t,uint32_t>> index_block_extrema;
  };
  std::map<uint64_t,Entry> live;
  ImmutableBufferCache<Diligent::RefCntAutoPtr<Diligent::IBuffer>> immutable{
      size_t{64}<<20,4096,size_t{1}<<20};
  uint64_t next_handle = 1;
};
DiligentMeshUploadBackend::DiligentMeshUploadBackend(Diligent::IRenderDevice* device)
    : state_(device ? std::make_unique<State>(device) : nullptr) {
  if (!device) throw std::invalid_argument("Missing native EoR Vulkan/Diligent device");
}
DiligentMeshUploadBackend::~DiligentMeshUploadBackend() noexcept = default;
uint64_t DiligentMeshUploadBackend::Create(const void* bytes, uint64_t length, uint32_t kind) {
  if (!bytes || !length || length > kMaxUploadBytes || kind>4u ||
      state_->next_handle == std::numeric_limits<uint64_t>::max()) return 0;
  Diligent::BufferDesc description;
  description.Name = kind == 0 ? "EoR decoded XYZ" :
                     kind == 1 ? "EoR original BE attribute stream" :
                     kind == 2 ? "EoR native index32" :
                     kind == 3 ? "EoR decoded native float4 attributes" :
                                 "EoR decoded native uint4 blend indices";
  description.Size = length;
  description.Usage = Diligent::USAGE_IMMUTABLE;
  // The original BE stream is not a verified host vertex layout. It is
  // uploaded and owned, but may not be submitted as a draw until decoded.
  description.BindFlags = kind == 2 ? Diligent::BIND_INDEX_BUFFER : Diligent::BIND_VERTEX_BUFFER;
  const Diligent::BufferData initial{bytes,length};
  try {
    Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
    const auto payload=std::span<const std::byte>(
        static_cast<const std::byte*>(bytes),static_cast<size_t>(length));
    // Raw Xbox attribute buffers are deliberately not cached here.
    // Only identical immutable host buffers of the same kind may share
    // a native allocation; every Draw keeps its own source handle.
    if(kind!=1u)
      if(const auto* old=state_->immutable.Find(kind,payload))buffer=*old;
    if(!buffer) {
      state_->device->CreateBuffer(description,&initial,&buffer);
      if(!buffer)return 0;
      if(kind!=1u)try {state_->immutable.Remember(kind,payload,buffer);}
                  catch (...) { /* cache is optional */ }
    }
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
  if (v.empty() || v.size() > kMaxUploadBytes / sizeof(uint32_t) ||
      v.size()%3 != 0)return 0;
  // Snapshot BEFORE GPU allocation; failure cannot leak a live handle.
  // The complete immutable mirror is used only for exact indexed windows,
  // not shader/texture emulation or guessed original Xbox streams.
  std::vector<uint32_t> mirror;
  std::vector<std::pair<uint32_t,uint32_t>> extrema;
  uint32_t minimum=UINT32_MAX,maximum=0;
  try {
    mirror.assign(v.begin(),v.end());
    extrema.reserve((mirror.size()-1)/kIndexBlockElements+1);
    for(size_t first=0;first<mirror.size();first+=kIndexBlockElements) {
      const size_t last=std::min(mirror.size(),first+kIndexBlockElements);
      const auto bounds=std::minmax_element(
          mirror.begin()+first,mirror.begin()+last);
      extrema.emplace_back(*bounds.first,*bounds.second);
      minimum=std::min(minimum,*bounds.first);
      maximum=std::max(maximum,*bounds.second);
    }
  } catch (...) {return 0;}
  const auto handle=Create(v.data(),v.size_bytes(),2);
  if (handle) {
    auto& record=state_->live.at(handle);
    record.minimum_index=minimum;
    record.maximum_index=maximum;
    record.immutable_indices=std::move(mirror);
    record.index_block_extrema=std::move(extrema);
  }
  return handle;
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
uint64_t DiligentMeshUploadBackend::UploadDecodedIntegerAttributes(
    std::span<const std::array<uint32_t,4>> values) {
  if(values.empty() || values.size()>kMaxUploadBytes/sizeof(values[0]))return 0;
  for(const auto& v:values)
    for(uint32_t index:v)
      if(index>255u)return 0;
  return Create(values.data(),values.size_bytes(),4);
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
uint32_t DiligentMeshUploadBackend::MaxIndexOf(uint64_t handle) const noexcept {
  const auto it=state_->live.find(handle);
  return it==state_->live.end() || it->second.kind!=2?
      UINT32_MAX:it->second.maximum_index;
}
uint32_t DiligentMeshUploadBackend::MinIndexOf(uint64_t handle) const noexcept {
  const auto it=state_->live.find(handle);
  return it==state_->live.end() || it->second.kind!=2 ?
      UINT32_MAX : it->second.minimum_index;
}
bool DiligentMeshUploadBackend::IndexWindowInRange(
    uint64_t handle,uint32_t first,uint32_t count,
    int32_t base,uint32_t vertex_count) const noexcept {
  const auto it=state_->live.find(handle);
  if(it==state_->live.end() || it->second.kind!=2 ||
     !count || count%3 || vertex_count<3)
    return false;
  const auto& indices=it->second.immutable_indices;
  if(first>indices.size() || count>indices.size()-first)
    return false;
  // Validate the exact immutable source window, not whole-buffer extrema.
  // Full blocks are cached; only two unaligned boundary fragments are
  // scanned on the live render thread.
  const size_t end=size_t(first)+count;
  uint32_t minimum=UINT32_MAX,maximum=0;
  auto merge=[&](uint32_t low,uint32_t high) noexcept {
    minimum=std::min(minimum,low);
    maximum=std::max(maximum,high);
  };
  size_t pos=first;
  for(;pos<end && pos%kIndexBlockElements!=0;++pos)
    merge(indices[pos],indices[pos]);
  for(;end-pos>=kIndexBlockElements;pos+=kIndexBlockElements) {
    const auto& bounds=it->second.index_block_extrema[pos/kIndexBlockElements];
    merge(bounds.first,bounds.second);
  }
  for(;pos<end;++pos)
    merge(indices[pos],indices[pos]);
  return int64_t(minimum)+base>=0 &&
         int64_t(maximum)+base<vertex_count;
}
size_t DiligentMeshUploadBackend::LiveBufferCount() const noexcept {return state_->live.size();}
} // namespace hulk::eor::gpu
