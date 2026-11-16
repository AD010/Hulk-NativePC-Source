#pragma once
// Strict host allocation boundary. Original raw attribute bytes remain in Xbox
// big-endian format; only XYZ has been decoded to host floats at this stage.
// No shader, texture sampler, mesh/material correspondence or Vulkan Draw is inferred.
#include "eor_native_mesh_geometry.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace hulk::eor::model {
struct UploadedNativeMesh {
  size_t original_header_offset=0;
  uint64_t position_buffer=0, original_vertex_stream_buffer=0, index_buffer=0;
  uint32_t original_vertex_stride=0, vertex_count=0, index_count=0;
  std::vector<std::array<std::byte,4>> original_attribute_declarations;
};
class NativeMeshUploadBackend {
 public:
  virtual ~NativeMeshUploadBackend() = default;
  // Each upload synchronously owns a full copy; returned live handles must be
  // unique across all buffer types. No source span survives the call.
  virtual uint64_t UploadPositions(std::span<const NativePosition>)=0;
  virtual uint64_t UploadOriginalVertexStream(std::span<const std::byte>)=0;
  virtual uint64_t UploadIndices(std::span<const uint32_t>)=0;
  virtual void ReleaseBuffer(uint64_t) noexcept=0;
};
class UploadedNativeMeshBatch {
 public:
  UploadedNativeMeshBatch()=default;
  UploadedNativeMeshBatch(const UploadedNativeMeshBatch&)=delete;
  UploadedNativeMeshBatch& operator=(const UploadedNativeMeshBatch&)=delete;
  UploadedNativeMeshBatch(UploadedNativeMeshBatch&&) noexcept;
  UploadedNativeMeshBatch& operator=(UploadedNativeMeshBatch&&) noexcept;
  ~UploadedNativeMeshBatch() noexcept;
  std::span<const UploadedNativeMesh> meshes() const noexcept {return meshes_;}
  void Reset() noexcept;
 private:
  friend UploadedNativeMeshBatch UploadNativeIndexedMeshes(std::span<const NativeIndexedMesh>,NativeMeshUploadBackend&);
  NativeMeshUploadBackend* backend_=nullptr; // backend must outlive this batch
  std::vector<UploadedNativeMesh> meshes_;
};
// Entire batch is validated before any allocation. Failure rolls back every
// successful allocation, including partial creation within the current mesh.
UploadedNativeMeshBatch UploadNativeIndexedMeshes(std::span<const NativeIndexedMesh>,NativeMeshUploadBackend&);
} // namespace hulk::eor::model
