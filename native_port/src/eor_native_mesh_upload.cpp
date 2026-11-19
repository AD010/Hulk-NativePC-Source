#include "eor_native_mesh_upload.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
namespace hulk::eor::model {
namespace {
void CheckMesh(const NativeIndexedMesh& mesh) {
  const size_t vertices=mesh.positions.size(),indices=mesh.triangle_list_indices.size();
  const uint32_t stride=mesh.original_vertex_stride,attrs=mesh.original_attribute_count;
  if (stride<12 || stride>192 || stride%4 || attrs<1 || attrs>16 ||
      mesh.original_attribute_declarations.size()!=attrs ||
      vertices<3 || vertices>UINT16_MAX || indices<3 || indices>UINT16_MAX || indices%3 ||
      mesh.original_vertex_stream.size()!=vertices*stride ||
      mesh.source_indices_offset<mesh.source_header_offset ||
      mesh.source_indices_offset-mesh.source_header_offset!=15u+size_t(attrs)*4 ||
      mesh.source_vertices_offset<mesh.source_indices_offset ||
      mesh.source_vertices_offset-mesh.source_indices_offset!=indices*2)
    throw std::invalid_argument("Invalid original EoR indexed mesh layout");
  for (uint32_t i:mesh.triangle_list_indices)
    if (i>=vertices) throw std::invalid_argument("Original EoR mesh index out of bounds");
  for (const auto& p:mesh.positions)
    if (!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||
        std::abs(p.x)>1e6f||std::abs(p.y)>1e6f||std::abs(p.z)>1e6f)
      throw std::invalid_argument("Invalid original EoR mesh position");
}
bool Exists(std::span<const UploadedNativeMesh> meshes,uint64_t h) noexcept {
  for(const auto& m:meshes)
    if(m.position_buffer==h || m.original_vertex_stream_buffer==h || m.index_buffer==h)
      return true;
    for(const uint64_t slot:m.decoded_attribute_buffers)if(slot==h)return true;
  return false;
}
} // namespace
UploadedNativeMeshBatch::UploadedNativeMeshBatch(UploadedNativeMeshBatch&& other) noexcept
 :backend_(std::exchange(other.backend_,nullptr)),meshes_(std::move(other.meshes_)) {other.meshes_.clear();}
UploadedNativeMeshBatch& UploadedNativeMeshBatch::operator=(UploadedNativeMeshBatch&& other) noexcept {
  if(this!=&other){Reset();backend_=std::exchange(other.backend_,nullptr);meshes_=std::move(other.meshes_);other.meshes_.clear();}
  return *this;
}
UploadedNativeMeshBatch::~UploadedNativeMeshBatch() noexcept {Reset();}
void UploadedNativeMeshBatch::Reset() noexcept {
  if(backend_) for(auto it=meshes_.rbegin();it!=meshes_.rend();++it){
    for(auto a=it->decoded_attribute_buffers.rbegin();a!=it->decoded_attribute_buffers.rend();++a)
      if(*a)backend_->ReleaseBuffer(*a);
    if(it->index_buffer) backend_->ReleaseBuffer(it->index_buffer);
    if(it->original_vertex_stream_buffer) backend_->ReleaseBuffer(it->original_vertex_stream_buffer);
    if(it->position_buffer) backend_->ReleaseBuffer(it->position_buffer);
  }
  meshes_.clear();backend_=nullptr;
}
UploadedNativeMeshBatch UploadNativeIndexedMeshes(
    std::span<const NativeIndexedMesh> meshes,NativeMeshUploadBackend& backend,
    std::span<const std::vector<VerifiedAttributeLayout>> layouts) {
  if(meshes.empty()) throw std::invalid_argument("No EoR indexed meshes to upload");
  if(!layouts.empty() && layouts.size()!=meshes.size())
    throw std::invalid_argument("Original attribute layouts do not match mesh count");
  // Validate and decode the ENTIRE original model batch before GPU allocation.
  std::vector<std::vector<std::vector<HostAttribute>>> decoded(meshes.size());
  for(size_t i=0;i<meshes.size();++i) {
    CheckMesh(meshes[i]);
    if(layouts.empty())continue;
    if(layouts[i].size()>15)
      throw std::invalid_argument("Too many source-proven host attribute slots");
    for(size_t j=0;j<layouts[i].size();++j) {
      for(size_t k=0;k<j;++k)
        if(layouts[i][j].original_byte_offset==layouts[i][k].original_byte_offset &&
           layouts[i][j].components==layouts[i][k].components &&
           layouts[i][j].encoding==layouts[i][k].encoding)
          throw std::invalid_argument("Duplicate original attribute declaration");
      decoded[i].push_back(DecodeVerifiedAttribute(meshes[i],layouts[i][j]));
    }
  }
  UploadedNativeMeshBatch batch;
  batch.meshes_.reserve(meshes.size()); // allocation failures occur before any GPU upload
  batch.backend_=&backend;
  for(size_t mesh_id=0;mesh_id<meshes.size();++mesh_id){
    const auto& m=meshes[mesh_id];
    // Material metadata is never guessed from the nearest preceding surface.
    UploadedNativeMesh rec;
    rec.original_header_offset=m.source_header_offset;
    rec.original_vertex_stride=m.original_vertex_stride;
    rec.vertex_count=uint32_t(m.positions.size());
    rec.index_count=uint32_t(m.triangle_list_indices.size());
    const auto limits=std::minmax_element(m.triangle_list_indices.begin(),
                                          m.triangle_list_indices.end());
    rec.min_index=*limits.first;
    rec.max_index=*limits.second;
    rec.host_indices=m.triangle_list_indices;
    rec.decoded_attribute_buffers.resize(decoded[mesh_id].size());
    rec.original_attribute_declarations=m.original_attribute_declarations;
    batch.meshes_.push_back(std::move(rec)); // bookkeeping first, then GPU ownership
    auto& o=batch.meshes_.back();
    const auto receive=[&](uint64_t h, uint64_t& slot) {
      if(!h) throw std::runtime_error("Native EoR geometry GPU buffer allocation failed");
      if(Exists(batch.meshes_,h))
        throw std::logic_error("Uploader returned a duplicate live GPU buffer handle");
      slot=h;
    };
    receive(backend.UploadPositions(m.positions),o.position_buffer);
    receive(backend.UploadOriginalVertexStream(m.original_vertex_stream),o.original_vertex_stream_buffer);
    receive(backend.UploadIndices(m.triangle_list_indices),o.index_buffer);
    for(size_t i=0;i<decoded[mesh_id].size();++i)
      receive(backend.UploadDecodedAttributes(decoded[mesh_id][i]),
              o.decoded_attribute_buffers[i]);
  }
  return batch;
}
} // namespace hulk::eor::model
