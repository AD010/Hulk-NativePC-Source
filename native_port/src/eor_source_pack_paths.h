#pragma once
#include <array>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace hulk::eor::source {
inline bool Regular(const std::filesystem::path& p) {
  std::error_code ec;
  return std::filesystem::is_regular_file(p,ec) && !ec;
}
inline bool HasEither(const std::filesystem::path& root,const char* a,const char* b) {
  return Regular(root/a) || Regular(root/b);
}
inline bool HasCompletePack(const std::filesystem::path& root) {
  return !root.empty() &&
    HasEither(root,"hulk_shader_bank.bin","HEORSPV1.bin") &&
    HasEither(root,"hulk_effect_ledger.bin","HEORMAT2.bin") &&
    HasEither(root,"hulk_shader_reflection.bin","HEORREF1.bin") &&
    HasEither(root,"hulk_sampler_bank.bin","HEORSMP1.bin") &&
    HasEither(root,"hulk_shader_virtual_fingerprints.bin","HEORVRT1.bin");
}
// One shared directory for application and Vulkan worker. An explicit
// override is authoritative; otherwise prefer a complete portable pack
// over any stale metadata-only cache. Never mix banks across directories.
inline std::filesystem::path ResolvePackRoot(
    const std::filesystem::path& cache,
    const std::filesystem::path& config_dir) {
  if(const char* custom=std::getenv("HULK_EOR_SOURCE_BANK_DIR");custom && *custom)
    return std::filesystem::path(custom);
  std::error_code ec;
  const auto cwd=std::filesystem::current_path(ec);
  const std::array roots{
      config_dir.empty()?std::filesystem::path{}:config_dir/"eor_offline",
      cache.empty()?std::filesystem::path{}:cache/"eor_offline",
      ec?std::filesystem::path{}:cwd/"eor_offline",
      cache.empty()?std::filesystem::path{}:cache/"eor_source"};
  for(const auto& root:roots)
    if(HasCompletePack(root))return root;
  for(const auto& root:roots)
    if(!root.empty() && HasEither(root,"hulk_shader_virtual_fingerprints.bin","HEORVRT1.bin"))
      return root;
  for(const auto& root:roots)if(!root.empty())return root;
  return {};
}
} // namespace hulk::eor::source
