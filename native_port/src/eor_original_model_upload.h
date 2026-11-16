#pragma once
// Bridge for a VERIFIED original IND/ARC model, its independent surface
// candidates, and the owned native geometry buffers. Does not map meshes to
// surfaces or claim that candidate texture IDs are shader sampler bindings.
#include "eor_original_model_assets.h"
#include "eor_native_mesh_upload.h"
#include <cstdint>
#include <vector>
namespace hulk::eor::model {
struct UploadedOriginalModel {
  uint32_t original_resource_hash=0;
  Identity original_identity;
  std::vector<SurfaceMaterials> original_surface_candidates;
  UploadedNativeMeshBatch meshes;
};
// Accept the decoded Xbox geometry produced by OriginalModelAssets.
// All model/surface metadata is checked before allocating any GPU buffers.
// Byte-heavy original source payloads are released on destruction of input.
UploadedOriginalModel UploadOriginalModel(LoadedNativeGeometry&& source, NativeMeshUploadBackend& backend);
}
