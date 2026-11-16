#pragma once
// Real user's original Xbox 360 IND/ARC resource source; no extracted
// proprietary game content is redistributed or embedded in the executable.
#include "eor_native_world_stream.h"
#include <filesystem>
namespace hulk::eor::world {
class ArchiveModelSource final : public ModelSource {
 public:
  ArchiveModelSource(const std::filesystem::path& original_index,
                     const std::filesystem::path& original_archive);
  std::optional<model::LoadedNativeGeometry> Load(uint32_t original_hash) override;
 private:
  model::OriginalModelAssets assets_;
};
}
