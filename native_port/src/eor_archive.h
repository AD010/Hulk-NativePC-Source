// The Incredible Hulk (2008), original Edge of Reality resource index.
// Native Windows/Linux C++20 component. No Xenia, ReXGlue, XDK or Vulkan.
// Resource bytes remain exclusively in the user's own game installation.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hulk::eor {

inline constexpr std::string_view kUsaGlobalIndexSha256 =
    "ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d";

enum class ByteOrder { Little, Big };

struct Resource {
  uint32_t hash = 0;
  uint32_t offset = 0;
  uint32_t size = 0;
  uint32_t reserved = 0;
};

// Actual 2008 EoR rule: CRC32(uppercase ASCII resource name).
// Authoring links TXFLui_* / LFXTui_* reference the same index key as ui_*.
uint32_t ResourceHash(std::string_view name);

class Index {
 public:
  static Index Decode(std::span<const std::byte> bytes);
  static Index Load(const std::filesystem::path& path);
  static Index LoadPinnedUsa(const std::filesystem::path& path);
  // Source-authenticated Xbox MODELS/Textures ONLY. The user's archived
  // indexXBOX360.ind (SHA 46e6c6ec...) and native port's pinned USA
  // INDEX.IND (SHA ad22432b...) have byte-for-byte identical Models
  // (4174) and Textures (6669) resource tables, although their other
  // archive group records differ. Keep the broader XDK/UI index
  // authenticator unchanged; only model GPU-stream I/O accepts both.
  static Index LoadPinnedUsaModelArchive(const std::filesystem::path& path);
  // Both SHA-pinned owner indices have exactly the same Models, Textures
  // and TexStreams records (4174, 6669, 5836 respectively). Graphics
  // loading may use either; other game-data consumers keep the stricter
  // global USA index requirement.
  static Index LoadPinnedUsaGraphicsArchive(const std::filesystem::path& path);

  ByteOrder byte_order() const noexcept { return byte_order_; }
  uint32_t footer() const noexcept { return footer_; }
  const std::map<std::string, std::vector<Resource>>& groups() const noexcept {
    return groups_;
  }
  std::optional<Resource> FindHash(std::string_view group, uint32_t hash) const;
  std::optional<Resource> FindName(std::string_view group,
                                    std::string_view resource_name) const;

 private:
  ByteOrder byte_order_ = ByteOrder::Little;
  uint32_t footer_ = 0;
  std::map<std::string, std::vector<Resource>> groups_;
};

// Exact, bounded resource payload; does not guess or transform its format.
// All file I/O stays native: no guest memory, callbacks, or global state.
std::vector<std::byte> ReadResource(const std::filesystem::path& archive,
                                    const Resource& resource,
                                    size_t maximum_size = 256u << 20);
}  // namespace hulk::eor
