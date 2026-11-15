#pragma once
// Resolve and load genuine EoR Models records from original IND/ARC.
// No Xenos runtime, PM4 commands, altered game bytes, or guessed GPU state.
#include "eor_archive.h"
#include "eor_model_resource.h"
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
class OriginalModelAssets {
 public:
  static OriginalModelAssets Open(const std::filesystem::path& index,
                                  const std::filesystem::path& archive);
  std::optional<LoadedModel> LoadHash(uint32_t hash) const;
  std::optional<LoadedModel> LoadName(std::string_view name) const;
  size_t IndexedModelCount() const noexcept;
 private:
  Index index_;
  std::filesystem::path archive_;
};
} // namespace hulk::eor::model
