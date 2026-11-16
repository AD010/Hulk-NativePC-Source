#pragma once
// Native DiligentCore (Vulkan) immutable GPU allocation backend for real EoR
// XYZ, original attributes and indexed topology. No Xenos guest GPU handles.
#include "eor_native_mesh_upload.h"
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
  void ReleaseBuffer(uint64_t handle) noexcept override;
  // Raw, borrowed pointer: valid only until ReleaseBuffer or destruction.
  Diligent::IBuffer* FindBuffer(uint64_t handle) const noexcept;
  size_t LiveBufferCount() const noexcept;
 private:
  struct State;
  std::unique_ptr<State> state_;
  uint64_t Create(const void* data, uint64_t bytes, uint32_t kind);
};
}
