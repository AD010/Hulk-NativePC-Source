#pragma once
// Resolve and load genuine EoR Models records from original IND/ARC.
// No Xenos runtime, PM4 commands, altered game bytes, or guessed GPU state.
#include "eor_archive.h"
#include "eor_model_resource.h"
#include "eor_model_surface_materials.h"
#include "eor_native_mesh_geometry.h"
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>
namespace hulk::eor::model {
struct LoadedModel {
  uint32_t original_resource_hash = 0;
  Identity source;
  std::vector<std::byte> bytes; // Owner's original data, not redistributed.
  bool name_hash_matches = false;
};
struct LoadedModelWithMaterials {
  LoadedModel model;
  std::vector<SurfaceMaterials> surfaces;
};
struct LoadedNativeGeometry {
  LoadedModel model;
  std::vector<SurfaceMaterials> original_surfaces;
  std::vector<NativeIndexedMesh> decoded_meshes;
  // No invented mesh -> material pass assignment; these are independent
  // original source arrays until the EoR material draw ABI is proven.
};
class OriginalModelAssets {
 public:
  static OriginalModelAssets Open(Index index,const std::filesystem::path& archive);
  static OriginalModelAssets Open(const std::filesystem::path& index,
                                  const std::filesystem::path& archive);
  std::optional<LoadedModel> LoadHash(uint32_t hash) const;
  std::optional<LoadedModel> LoadName(std::string_view name) const;
  // Full direct original-game Model -> ALL material surfaces -> indexed
  // texture hashes. Still no claimed vertex/index stream or Vulkan PSO.
  std::optional<LoadedModelWithMaterials> LoadWithMaterialsHash(uint32_t hash) const;
  std::optional<LoadedModelWithMaterials> LoadWithMaterialsName(std::string_view name) const;
  // Direct original Xbox IND/ARC -> all surface descriptors -> validated
  // host-native position/index buffers. No guest PM4 renderer is used.
  std::optional<LoadedNativeGeometry> LoadNativeGeometryHash(uint32_t hash) const;
  std::optional<LoadedNativeGeometry> LoadNativeGeometryName(std::string_view name) const;
  size_t IndexedModelCount() const noexcept;
 private:
  Index index_;
  std::filesystem::path archive_;
};
} // namespace hulk::eor::model
