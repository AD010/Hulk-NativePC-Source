#include "eor_native_mesh_upload.h"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace hulk::eor::model {
namespace {
void CheckMesh(const NativeIndexedMesh& mesh) {
  const auto vertices = mesh.positions.size();
  const auto indices = mesh.triangle_list_indices.size();
  if (mesh.original_vertex_stride < 12 || mesh.original_vertex_stride > 192 ||
      mesh.original_vertex_stride % 4 ||
      mesh.original_attribute_count < 1 || mesh.original_attribute_count > 16 ||
      mesh.original_attribute_declarations.size() != mesh.original_attribute_count ||
      vertices < 3 || vertices > UINT16_MAX ||
      indices < 3 || indices > UINT16_MAX || indices % 3 != 0 ||
      mesh.original_vertex_stream.size() != vertices * mesh.original_vertex_stride ||
      mesh.source_indices_offset < mesh.source_header_offset ||
      mesh.source_vertices_offset < mesh.source_indices_offset)
    throw std::invalid_argument("Invalid original EoR mesh vertex/index layout");
  for (const auto i : mesh.triangle_list_indices)
    if (i >= vertices)
      throw std::invalid_argument("EoR index outside original vertex buffer");
  for (const auto& p : mesh.positions)
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
        std::abs(p.x) > 1e6f || std::abs(p.y) > 1e6f || std::abs(p.z) > 1e6f)
      throw std::invalid_argument("Non-finite/out-of-range original EoR XYZ");
}
bool HasHandle(std::span<const UploadedNativeMesh> meshes, uint64_t handle) {
  for (const auto& m : meshes)
    if (m.position_buffer == handle || m.index_buffer == handle)
      return true;
  return false;
}
} // namespace

UploadedNativeMeshBatch::UploadedNativeMeshBatch(UploadedNativeMeshBatch&& other) noexcept
    : backend_(std::exchange(other.backend_, nullptr)),
      meshes_(std::move(other.meshes_)) { other.meshes_.clear(); }
UploadedNativeMeshBatch& UploadedNativeMeshBatch::operator=(UploadedNativeMeshBatch&& other) noexcept {
  if (this != &other) {
    Reset();
    backend_ = std::exchange(other.backend_, nullptr);
    meshes_ = std::move(other.meshes_);
    other.meshes_.clear();
  }
  return *this;
}
UploadedNativeMeshBatch::~UploadedNativeMeshBatch() noexcept { Reset(); }
void UploadedNativeMeshBatch::Reset() noexcept {
  if (backend_) {
    for (auto it = meshes_.rbegin(); it != meshes_.rend(); ++it) {
      if (it->index_buffer) backend_->ReleaseBuffer(it->index_buffer);
      if (it->position_buffer) backend_->ReleaseBuffer(it->position_buffer);
    }
  }
  meshes_.clear();
  backend_ = nullptr;
}
UploadedNativeMeshBatch UploadNativeIndexedMeshes(
    std::span<const NativeIndexedMesh> meshes, NativeMeshUploadBackend& backend) {
  if (meshes.empty()) throw std::invalid_argument("No original EoR meshes to upload");
  // Reject malformed LATE meshes before touching GPU at all.
  for (const auto& m : meshes) CheckMesh(m);
  UploadedNativeMeshBatch result;
  result.meshes_.reserve(meshes.size());
  result.backend_ = &backend;
  for (const auto& m : meshes) {
    const uint64_t positions = backend.UploadPositions(m.positions);
    if (!positions) throw std::runtime_error("Cannot upload native EoR XYZ buffer");
    if (HasHandle(result.meshes_, positions))
      throw std::logic_error("Native GPU uploader reused a live buffer handle");
    result.meshes_.push_back({m.source_header_offset, positions, 0,
                             uint32_t(m.positions.size()),
                             uint32_t(m.triangle_list_indices.size())});
    const uint64_t indices = backend.UploadIndices(m.triangle_list_indices);
    if (!indices) throw std::runtime_error("Cannot upload native EoR index buffer");
    if (HasHandle(result.meshes_, indices))
      throw std::logic_error("Native GPU uploader reused a live buffer handle");
    result.meshes_.back().index_buffer = indices;
  }
  return result;
}
} // namespace hulk::eor::model
