// Original EoR UI sources for a future game-native menu renderer.
// NO unverified Xbox texture substitution is performed.
#include "eor_ui_asset_catalog.h"

#include <atomic>
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace hulk::eor {
namespace {
bool HasPrefix(std::span<const std::byte> bytes,
               const unsigned char* prefix, size_t size) noexcept {
  if (bytes.size() < size) return false;
  for (size_t i = 0; i != size; ++i) {
    if (std::to_integer<unsigned char>(bytes[i]) != prefix[i]) return false;
  }
  return true;
}

std::filesystem::path ArchiveFor(const OriginalArchiveSet& set,
                                 std::string_view group) {
  if (group == "Fonts") return set.fonts;
  if (group == "Textures") return set.textures;
  // Do not guess the location of user-supplied Objects, Datasets or other
  // platform-specific archives. They need explicit separate bindings.
  return {};
}
}  // namespace

KnownImageEncoding ClassifyImage(std::span<const std::byte> data) noexcept {
  static constexpr unsigned char DDS[] = {'D', 'D', 'S', ' '};
  static constexpr unsigned char PNG[] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
  static constexpr unsigned char BMP[] = {'B', 'M'};
  if (HasPrefix(data, DDS, sizeof DDS)) return KnownImageEncoding::Dds;
  if (HasPrefix(data, PNG, sizeof PNG)) return KnownImageEncoding::Png;
  if (HasPrefix(data, BMP, sizeof BMP)) return KnownImageEncoding::Bmp;
  return KnownImageEncoding::Unknown;
}

OriginalUiAssets::OriginalUiAssets(
    OriginalArchiveSet pc, Index pc_index,
    std::optional<OriginalArchiveSet> xbox,
    std::optional<Index> xbox_index)
    : pc_(std::move(pc)), pc_index_(std::move(pc_index)),
      xbox_(std::move(xbox)), xbox_index_(std::move(xbox_index)) {}

OriginalUiAssets OriginalUiAssets::Open(
    OriginalArchiveSet pc, std::optional<OriginalArchiveSet> xbox) {
  // Mixing Xbox endianness with a PC archive can silently look plausible
  // when a few resource IDs happen to match. Reject the wrong pair early.
  auto pc_index = Index::Load(pc.index);
  if (pc_index.byte_order() != ByteOrder::Little)
    throw std::runtime_error("Original PC Hulk index must be little-endian");
  std::optional<Index> xbox_index;
  if (xbox) {
    xbox_index = Index::Load(xbox->index);
    if (xbox_index->byte_order() != ByteOrder::Big)
      throw std::runtime_error("Original Xbox 360 Hulk index must be big-endian");
  }
  return OriginalUiAssets(std::move(pc), std::move(pc_index),
                          std::move(xbox), std::move(xbox_index));
}

std::optional<OriginalUiAsset> OriginalUiAssets::Find(
    OriginalPlatform platform, std::string_view category,
    std::string_view original_name) const {
  const auto* index = &pc_index_;
  const auto* archive_set = &pc_;
  if (platform == OriginalPlatform::Xbox360) {
    if (!xbox_index_ || !xbox_) return std::nullopt;
    index = &*xbox_index_;
    archive_set = &*xbox_;
  }
  const auto archive = ArchiveFor(*archive_set, category);
  if (archive.empty()) return std::nullopt;
  const auto entry = index->FindName(category, original_name);
  if (!entry) return std::nullopt;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(archive, ec) || ec) return std::nullopt;
  const auto size = std::filesystem::file_size(archive, ec);
  if (ec || entry->offset < 4 || entry->size == 0 ||
      static_cast<uint64_t>(entry->offset) + entry->size > size)
    return std::nullopt;
  return OriginalUiAsset{platform, std::string(category),
                         std::string(original_name), archive, *entry};
}

std::vector<std::byte> OriginalUiAssets::Read(
    const OriginalUiAsset& asset, size_t maximum) const {
  // Revalidate both origin and the actual index entry: callers may not
  // fabricate an Xbox asset with a PC path or changed bounds.
  auto verified = Find(asset.platform, asset.group, asset.name);
  if (!verified || verified->archive != asset.archive ||
      verified->record.hash != asset.record.hash ||
      verified->record.offset != asset.record.offset ||
      verified->record.size != asset.record.size ||
      verified->record.reserved != asset.record.reserved)
    throw std::runtime_error("UI resource origin or indexed range changed");
  return ReadResource(verified->archive, verified->record, maximum);
}

std::vector<std::vector<std::byte>> OriginalUiAssets::ReadBatch(
    std::span<const OriginalUiAsset> assets, size_t maximum_total,
    size_t maximum_each, uint32_t workers) const {
  if (assets.empty()) return {};
  if (assets.size() > 4096 || maximum_total == 0 ||
      maximum_total > (256u << 20) ||
      maximum_each == 0 || maximum_each > maximum_total ||
      workers > 16)
    throw std::invalid_argument("Invalid native asset-batch CPU/memory budget");
  size_t total = 0;
  // Complete the size and origin preflight before starting any I/O.
  for (const auto& asset : assets) {
    if (asset.record.size == 0 || asset.record.size > maximum_each ||
        asset.record.size > maximum_total - total)
      throw std::invalid_argument("Native EoR asset read exceeds host memory budget");
    auto found = Find(asset.platform, asset.group, asset.name);
    if (!found || found->archive != asset.archive ||
        found->record.hash != asset.record.hash ||
        found->record.offset != asset.record.offset ||
        found->record.size != asset.record.size ||
        found->record.reserved != asset.record.reserved)
      throw std::invalid_argument("Unverified EoR asset in parallel read batch");
    total += asset.record.size;
  }
  const unsigned logical = std::max(1u, std::thread::hardware_concurrency());
  const unsigned background_workers = std::min<unsigned>(
      static_cast<unsigned>(assets.size()),
      std::min<unsigned>(8u, workers ? workers :
          std::max(1u, logical > 2 ? logical - 2 : 1u)));
  std::vector<std::vector<std::byte>> output(assets.size());
  std::vector<std::exception_ptr> failures(assets.size());
  std::atomic_size_t next{0};
  {
    std::vector<std::jthread> threads;
    threads.reserve(background_workers);
    for (unsigned w = 0; w < background_workers; ++w) {
      threads.emplace_back([&] {
#ifdef _WIN32
        // Asset prefetch must not preempt the game's audio/physics threads.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
        for (;;) {
          const size_t i = next.fetch_add(1, std::memory_order_relaxed);
          if (i >= assets.size()) break;
          try {
            output[i] = Read(assets[i], maximum_each);
          } catch (...) {
            failures[i] = std::current_exception();
          }
        }
      });
    }
  }  // joins every jthread; no workers can outlive assets or the catalogue.
  for (const auto& error : failures)
    if (error) std::rethrow_exception(error);
  return output;
}

}  // namespace hulk::eor
