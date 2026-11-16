#pragma once
// Host GPU buffer upload boundary for genuine decoded EoR Xbox meshes.
// No Xenos PM4, material inference or placeholder vertex/pixel shaders.
#include "eor_native_mesh_geometry.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace hulk::eor::model {
struct UploadedNativeMesh {
  size_t original_header_offset = 0;
  uint64_t position_buffer = 0;
  uint64_t index_buffer = 0;
  uint32_t vertex_count = 0;
  uint32_t index_count = 0;
};

class NativeMeshUploadBackend {
 public:
  virtual ~NativeMeshUploadBackend() = default;
  // Must synchronously copy the supplied source bytes to a host-owned
  // allocation. The spans are only valid for the duration of the call.
  // Zero means failure; handles must be unique among live allocations.
  virtual uint64_t UploadPositions(std::span<const NativePosition>) = 0;
  virtual uint64_t UploadIndices(std::span<const uint32_t>) = 0;
  virtual void ReleaseBuffer(uint64_t handle) noexcept = 0;
};

// Move-only owner of GPU allocations. The backend must outlive this batch.
// No draw may be submitted until EoR material-to-mesh and shader binding
// are independently proven; this stage only creates honest native buffers.
class UploadedNativeMeshBatch {
 public:
  UploadedNativeMeshBatch() = default;
  UploadedNativeMeshBatch(const UploadedNativeMeshBatch&) = delete;
  UploadedNativeMeshBatch& operator=(const UploadedNativeMeshBatch&) = delete;
  UploadedNativeMeshBatch(UploadedNativeMeshBatch&& other) noexcept;
  UploadedNativeMeshBatch& operator=(UploadedNativeMeshBatch&& other) noexcept;
  ~UploadedNativeMeshBatch() noexcept;
  std::span<const UploadedNativeMesh> meshes() const noexcept { return meshes_; }
  void Reset() noexcept;
 private:
  friend UploadedNativeMeshBatch UploadNativeIndexedMeshes(
      std::span<const NativeIndexedMesh>, NativeMeshUploadBackend&);
  NativeMeshUploadBackend* backend_ = nullptr;
  std::vector<UploadedNativeMesh> meshes_;
};

// Entire batch is CPU-preflighted BEFORE the first GPU allocation.
// On any failure all buffers created by this call are released, even if
// the second buffer or a later mesh fails. Never fabricates material IDs.
UploadedNativeMeshBatch UploadNativeIndexedMeshes(
    std::span<const NativeIndexedMesh> meshes, NativeMeshUploadBackend& backend);
} // namespace hulk::eor::model
