#pragma once
// Strict host allocation boundary. Original raw attribute bytes remain in Xbox
// big-endian format; only XYZ has been decoded to host floats at this stage.
// No shader, texture sampler, mesh/material correspondence or Vulkan Draw is inferred.
#include "eor_native_attributes.h"
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
  uint32_t min_index=UINT32_MAX, max_index=UINT32_MAX;
  // Copy of the original verified host-decoded index32 stream; a Draw can
  // reference only a subrange with a different signed BaseVertex. Global
  // min/max must NOT invalidate legal local subranges.
  std::vector<uint32_t> host_indices;
  // Native float4 slots decoded from verified Xbox attribute layouts.
  std::vector<uint64_t> decoded_attribute_buffers;
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
  // Fail closed unless this backend can upload host-native decoded slots.
  virtual uint64_t UploadDecodedAttributes(std::span<const HostAttribute>) {return 0;}
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
  friend UploadedNativeMeshBatch UploadNativeIndexedMeshes(std::span<const NativeIndexedMesh>,NativeMeshUploadBackend&,
      std::span<const std::vector<VerifiedAttributeLayout>>);
  NativeMeshUploadBackend* backend_=nullptr; // backend must outlive this batch
  std::vector<UploadedNativeMesh> meshes_;
};
// Entire batch is validated before any allocation. Failure rolls back every
// successful allocation, including partial creation within the current mesh.
// Layouts are explicitly source-proven per mesh, never guessed from shader names.
// Every attribute is decoded before any GPU upload; empty means XYZ-only.
UploadedNativeMeshBatch UploadNativeIndexedMeshes(
    std::span<const NativeIndexedMesh> meshes,NativeMeshUploadBackend& backend,
    std::span<const std::vector<VerifiedAttributeLayout>> layouts={});
} // namespace hulk::eor::model
