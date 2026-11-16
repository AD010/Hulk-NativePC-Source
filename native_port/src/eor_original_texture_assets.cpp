#include "eor_original_texture_assets.h"
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
namespace hulk::eor::textures {
namespace {
constexpr size_t kMaxSingleTexture = 64u << 20;
constexpr uint64_t kMaxBatchBytes = 256ull << 20;
constexpr size_t kMaxCandidateSlots = 65536;
bool SameResource(const Resource& a, const Resource& b) {
  return a.hash==b.hash && a.offset==b.offset &&
         a.size==b.size && a.reserved==b.reserved;
}
}
OriginalXboxTextureAssets OriginalXboxTextureAssets::Open(
    const std::filesystem::path& index_path,
    const std::filesystem::path& archive_path,
    const std::filesystem::path& stream_path) {
  if (archive_path.empty() || !std::filesystem::is_regular_file(archive_path))
    throw std::invalid_argument("Original Xbox Textures.ARC is missing");
  OriginalXboxTextureAssets result;
  result.index_ = Index::Load(index_path);
  if (result.index_.byte_order() != ByteOrder::Big ||
      !result.index_.groups().contains("Textures"))
    throw std::invalid_argument("Original Xbox EoR texture index/platform mismatch");
  if (!stream_path.empty()) {
    if (!result.index_.groups().contains("TexStreams") ||
        !std::filesystem::is_regular_file(stream_path))
      throw std::invalid_argument("Original Xbox TexStreams.ARC is unavailable");
    result.stream_archive_ = stream_path;
  }
  result.archive_ = archive_path;
  return result;
}
std::optional<OriginalTexture> OriginalXboxTextureAssets::LoadHash(uint32_t hash) const {
  const auto indexed = index_.FindHash("Textures",hash);
  if (!indexed) return std::nullopt;
  if (!indexed->hash || indexed->reserved || !indexed->size ||
      indexed->size > kMaxSingleTexture)
    throw std::invalid_argument("Unsafe original Xbox texture record");
  auto bytes=ReadResource(archive_,*indexed,kMaxSingleTexture);
  OriginalTexture result{hash,*indexed,std::move(bytes),std::nullopt,{},
                         pinned_usa_index_};
  // Each original Xbox LFXT owns an optional variable-length inline data
  // section. This is source-evidenced by two nested BE size-prefix fields;
  // no synthetic mip offset or 52-byte texture-prefix removal is permitted.
  result.original_inline_section=FindOriginalLfxtInlineSection(
      result.original_bytes,hash);
  // A missing high-detail archive NEVER triggers guessed/upsampled mip data.
  if (index_.groups().contains("TexStreams")) {
    result.original_stream = FindVerifiedOriginalTexStream(
        result.original_bytes,hash,index_);
    if(result.original_stream && result.original_inline_section)
      result.original_stream_table=ParseOriginalLfxtStreamTable(
          result.original_bytes,*result.original_stream);
    if (result.original_stream && !stream_archive_.empty())
      result.original_stream_bytes = ReadOriginalTexStream(
          stream_archive_,*result.original_stream);
  }
  return result;
}
OriginalTextureBatch OriginalXboxTextureAssets::LoadSurfaceCandidates(
    std::span<const model::SurfaceMaterials> surfaces) const {
  OriginalTextureBatch batch;
  batch.surfaces.reserve(surfaces.size());
  // Preflight all records, size budgets, ordinal ordering and original slots
  // before opening a single ARC texture resource.
  std::map<uint32_t,Resource> unique;
  size_t total_slots=0;
  uint64_t total_bytes=0;
  for(size_t i=0;i<surfaces.size();++i) {
    const auto& s=surfaces[i];
    if (s.ordinal!=i || s.original_material_name.empty())
      throw std::invalid_argument("Unverified original Xbox EoR surface metadata");
    if (s.indexed_texture_references.size()>kMaxCandidateSlots-total_slots)
      throw std::invalid_argument("Too many original EoR texture candidate slots");
    total_slots+=s.indexed_texture_references.size();
    SurfaceCandidates out;
    out.ordinal=s.ordinal;
    out.original_material_name=s.original_material_name;
    out.candidate_slots.reserve(s.indexed_texture_references.size());
    for(const auto& ref:s.indexed_texture_references) {
      const auto indexed=index_.FindHash("Textures",ref.original_texture_hash);
      if(!indexed || !ref.original_texture_hash || !SameResource(*indexed,ref.original_resource) ||
         indexed->reserved || indexed->size==0 || indexed->size>kMaxSingleTexture)
        throw std::invalid_argument("Texture candidate does not match original Xbox IND");
      if(unique.emplace(ref.original_texture_hash,*indexed).second) {
        total_bytes+=indexed->size;
        if(total_bytes>kMaxBatchBytes)
          throw std::invalid_argument("Original EoR material texture batch exceeds memory budget");
      }
      out.candidate_slots.push_back({ref.offset_from_surface_data,ref.original_texture_hash});
    }
    batch.surfaces.push_back(std::move(out));
  }
  batch.unique_textures.reserve(unique.size());
  for(const auto& [hash,resource]:unique) {
    auto original=LoadHash(hash);
    if(!original || !SameResource(original->source_record,resource))
      throw std::logic_error("Indexed original EoR texture changed during load");
    if (original->original_stream_bytes.size() > kMaxBatchBytes-total_bytes)
      throw std::invalid_argument("Original texture stream batch exceeds memory budget");
    total_bytes += original->original_stream_bytes.size();
    batch.unique_textures.push_back(std::move(*original));
  }
  return batch;
}
} // namespace hulk::eor::textures
