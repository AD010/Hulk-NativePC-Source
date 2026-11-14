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
