#include "eor_original_model_upload.h"
#include <stdexcept>
#include <utility>
namespace hulk::eor::model {
UploadedOriginalModel UploadOriginalModel(LoadedNativeGeometry&& source, NativeMeshUploadBackend& backend) {
  if (source.model.source.platform!=Platform::Xbox360 ||
      source.model.source.model_name.empty() ||
      source.original_surfaces.size()!=source.model.source.surface_records ||
      source.decoded_meshes.empty())
    throw std::invalid_argument("Unverified original EoR Xbox model/surface directory");
  for(size_t i=0;i<source.original_surfaces.size();++i) {
    const auto& s=source.original_surfaces[i];
    if(s.ordinal!=i || s.original_material_name.empty() ||
       s.material_data_offset>=source.model.bytes.size())
      throw std::invalid_argument("Original EoR surface candidate metadata invalid");
    for(const auto& ref:s.indexed_texture_references)
      if(!ref.original_texture_hash || ref.original_resource.hash!=ref.original_texture_hash ||
         !ref.original_resource.size || ref.original_resource.reserved)
        throw std::invalid_argument("Original EoR indexed texture candidate inconsistent");
  }
  auto uploaded=UploadNativeIndexedMeshes(source.decoded_meshes,backend);
  return {source.model.original_resource_hash,
          std::move(source.model.source),
          std::move(source.original_surfaces),
          std::move(uploaded)};
}
} // namespace hulk::eor::model
