#pragma once
// Native DiligentCore (Vulkan) immutable GPU allocation backend for real EoR
// XYZ, original attributes and indexed topology. No Xenos guest GPU handles.
#include "eor_native_mesh_upload.h"
#include "eor_native_attributes.h"
#include <cstddef>
#include <cstdint>
#include <memory>
namespace Diligent { struct IRenderDevice; struct IBuffer; }
namespace hulk::eor::gpu {
class DiligentMeshUploadBackend final : public model::NativeMeshUploadBackend {
 public:
  explicit DiligentMeshUploadBackend(Diligent::IRenderDevice* device);
  DiligentMeshUploadBackend(const DiligentMeshUploadBackend&)=delete;
  DiligentMeshUploadBackend& operator=(const DiligentMeshUploadBackend&)=delete;
  ~DiligentMeshUploadBackend() noexcept override;
  uint64_t UploadPositions(std::span<const model::NativePosition>) override;
  uint64_t UploadOriginalVertexStream(std::span<const std::byte>) override;
  uint64_t UploadIndices(std::span<const uint32_t>) override;
  // Upload EoR attributes AFTER source-proven BE decoding. The raw Xbox
  // interleaved vertex stream above is never directly a host vertex layout.
  uint64_t UploadDecodedAttributes(std::span<const model::HostAttribute> values) override;
  void ReleaseBuffer(uint64_t handle) noexcept override;
  // Raw, borrowed pointer: valid only until ReleaseBuffer or destruction.
  Diligent::IBuffer* FindBuffer(uint64_t handle) const noexcept;
  // 0: decoded XYZ, 1: RAW big-endian Xbox (never bind), 2: native index32,
  // 3: decoded float4 attributes; UINT32_MAX: missing/stale handle.
  uint32_t KindOf(uint64_t handle) const noexcept;
  // Highest source index in this immutable GPU index buffer; UINT32_MAX
  // denotes a missing/non-index buffer, not an available vertex index.
  uint32_t MaxIndexOf(uint64_t handle) const noexcept;
  uint32_t MinIndexOf(uint64_t handle) const noexcept;
  // Actual immutable index window, not the global index-buffer extrema.
  // Verifies a signed base against the owning mesh's vertex count.
  bool IndexWindowInRange(uint64_t handle,uint32_t first,uint32_t count,
                          int32_t base,uint32_t vertex_count) const noexcept;
  size_t LiveBufferCount() const noexcept;
 private:
  struct State;
  std::unique_ptr<State> state_;
  uint64_t Create(const void* data, uint64_t bytes, uint32_t kind);
};
}
