#pragma once
// Hulk 2008 native EoR FMOD Event trailing catalog, no FMOD runtime loaded.
// The three per-record metadata words are intentionally opaque.
#include "eor_archive.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
namespace hulk::eor::fmod {
struct CatalogEntry {
  Resource resource;
  std::string identifier;
  std::array<uint32_t,3> opaque;
};
std::vector<CatalogEntry> DecodeCatalog(std::span<const std::byte> archive,
                                        std::span<const Resource> indexed);
std::vector<CatalogEntry> ReadCatalog(const Index& xbox_index,
                                     const std::filesystem::path& archive);
}
