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
  struct IndexMetadata {
    uint32_t maximum_index=UINT32_MAX;
    uint32_t minimum_index=UINT32_MAX;
    std::vector<uint32_t> immutable_indices;
    std::vector<std::pair<uint32_t,uint32_t>> index_block_extrema;
  };
  struct Entry {
    Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
    uint32_t kind=UINT32_MAX;
    std::shared_ptr<const IndexMetadata> indices;
  };
  std::map<uint64_t,Entry> live;
  // The byte-exact immutable GPU cache already authenticates the complete
  // index payload. All logical handles on that physical buffer can share
  // the same immutable CPU bounds. Weak entries never extend GPU lifetime.
  std::map<Diligent::IBuffer*,std::weak_ptr<const IndexMetadata>> index_metadata;
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
  return UploadPrimitiveIndices(v,false);
}
uint64_t DiligentMeshUploadBackend::UploadPointIndices(std::span<const uint32_t> v) {
  return UploadPrimitiveIndices(v,true);
}
uint64_t DiligentMeshUploadBackend::UploadPrimitiveIndices(std::span<const uint32_t> v,bool point_list) {
  if (v.empty() || v.size() > kMaxUploadBytes / sizeof(uint32_t) ||
      (!point_list && v.size()%3 != 0))return 0;
  const auto handle=Create(v.data(),v.size_bytes(),2);
  if(!handle)return 0;
  try {
    auto& record=state_->live.at(handle);
    auto* physical=record.buffer.RawPtr();
    if(const auto old=state_->index_metadata.find(physical);old!=state_->index_metadata.end()) {
      if(auto shared=old->second.lock()) {
        record.indices=std::move(shared);
        return handle;
      }
    }
    auto metadata=std::make_shared<State::IndexMetadata>();
    auto& mirror=metadata->immutable_indices;
    auto& extrema=metadata->index_block_extrema;
    mirror.assign(v.begin(),v.end());
    extrema.reserve((mirror.size()-1)/kIndexBlockElements+1);
    uint32_t minimum=UINT32_MAX,maximum=0;
    for(size_t first=0;first<mirror.size();first+=kIndexBlockElements) {
      const size_t last=std::min(mirror.size(),first+kIndexBlockElements);
      const auto bounds=std::minmax_element(mirror.begin()+first,mirror.begin()+last);
      extrema.emplace_back(*bounds.first,*bounds.second);
      minimum=std::min(minimum,*bounds.first);
      maximum=std::max(maximum,*bounds.second);
    }
    metadata->minimum_index=minimum;
    metadata->maximum_index=maximum;
    record.indices=metadata;
    state_->index_metadata.insert_or_assign(physical,metadata);
    return handle;
  } catch (...) {
    // The logical handle was never published to a Draw. Roll it back.
    // The optional immutable GPU cache may retain the physical allocation.
    ReleaseBuffer(handle);
    return 0;
  }
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
  const auto it=state_->live.find(handle);
  if(it==state_->live.end())return;
  auto* physical=it->second.buffer.RawPtr();
  if(it->second.kind==2u) {
    const auto metadata=state_->index_metadata.find(physical);
    // Erase weak keys before the last logical owner can release the GPU
    // address. A recycled pointer must never inherit earlier index bounds.
    if(metadata!=state_->index_metadata.end() &&
       (!it->second.indices || it->second.indices.use_count()==1))
      state_->index_metadata.erase(metadata);
  }
  state_->live.erase(it);
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
  return it==state_->live.end() || it->second.kind!=2 || !it->second.indices?
      UINT32_MAX:it->second.indices->maximum_index;
}
uint32_t DiligentMeshUploadBackend::MinIndexOf(uint64_t handle) const noexcept {
  const auto it=state_->live.find(handle);
  return it==state_->live.end() || it->second.kind!=2 || !it->second.indices ?
      UINT32_MAX : it->second.indices->minimum_index;
}
bool DiligentMeshUploadBackend::IndexWindowInRange(
    uint64_t handle,uint32_t first,uint32_t count,
    int32_t base,uint32_t vertex_count,bool point_list) const noexcept {
  const auto it=state_->live.find(handle);
  if(it==state_->live.end() || it->second.kind!=2 ||
     !count || !vertex_count || (!point_list && (count%3 || vertex_count<3)))
    return false;
  if(!it->second.indices)return false;
  const auto& metadata=*it->second.indices;
  const auto& indices=metadata.immutable_indices;
  if(first>indices.size() || count>indices.size()-first)
    return false;
  if(!first && count==indices.size())
    return int64_t(metadata.minimum_index)+base>=0 &&
           int64_t(metadata.maximum_index)+base<vertex_count;
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
    const auto& bounds=metadata.index_block_extrema[pos/kIndexBlockElements];
    merge(bounds.first,bounds.second);
  }
  for(;pos<end;++pos)
    merge(indices[pos],indices[pos]);
  return int64_t(minimum)+base>=0 &&
         int64_t(maximum)+base<vertex_count;
}
size_t DiligentMeshUploadBackend::LiveBufferCount() const noexcept {return state_->live.size();}
} // namespace hulk::eor::gpu
