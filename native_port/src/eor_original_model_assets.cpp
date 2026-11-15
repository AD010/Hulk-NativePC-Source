#include "eor_original_model_assets.h"
#include <stdexcept>
#include <utility>
namespace hulk::eor::model {
OriginalModelAssets OriginalModelAssets::Open(const std::filesystem::path& index,
                                               const std::filesystem::path& archive) {
  if (archive.empty() || !std::filesystem::is_regular_file(archive))
    throw std::invalid_argument("Original EoR MODELS.ARC unavailable");
  OriginalModelAssets assets;
  assets.index_ = Index::Load(index);
  if (assets.index_.groups().find("Models") == assets.index_.groups().end())
    throw std::invalid_argument("Models group absent in original EoR IND");
  assets.archive_ = archive;
  return assets;
}
std::optional<LoadedModel> OriginalModelAssets::LoadHash(uint32_t hash) const {
  const auto item = index_.FindHash("Models",hash);
  if (!item) return std::nullopt;
  auto bytes=ReadResource(archive_,*item,32u<<20);
  auto id=Inspect(bytes);
  if ((index_.byte_order()==ByteOrder::Big && id.platform!=Platform::Xbox360) ||
      (index_.byte_order()==ByteOrder::Little && id.platform!=Platform::PcD3d9))
    throw std::invalid_argument("Original model platform/index mismatch");
  const bool exact=ResourceHash(id.model_name)==hash;
  // Verified one exceptional alias exists in BOTH original platform IND:
  // hulk_front-end_model has a non-name CRC32 resource hash.
  if (!exact && !(hash==0x21e3a96cu && id.model_name=="hulk_front-end_model"))
    throw std::invalid_argument("Original EoR model resource hash/name mismatch");
  return LoadedModel{hash,std::move(id),std::move(bytes),exact};
}
std::optional<LoadedModel> OriginalModelAssets::LoadName(std::string_view name) const {
  // Special non-CRC frontend alias remains accessible through LoadHash.
  return LoadHash(ResourceHash(name));
}
size_t OriginalModelAssets::IndexedModelCount() const noexcept {
  const auto it=index_.groups().find("Models");
  return it==index_.groups().end()?0:it->second.size();
}
} // namespace hulk::eor::model
