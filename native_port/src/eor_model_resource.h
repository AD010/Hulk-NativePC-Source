#pragma once
// Real original EoR model header / first surface decoder. NOT vertex decoding.
// No Xenos, ReXGlue, Vulkan or original proprietary asset bytes required.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
namespace hulk::eor::model {
enum class Platform : uint8_t { PcD3d9, Xbox360 };
struct Identity {
  Platform platform = Platform::Xbox360;
  std::string model_name;
  uint8_t model_kind = 0; // 0 regular; 3 level aggregate in verified archives.
  uint32_t surface_records = 0;
  std::string first_surface_name;
  size_t surface_data_offset = 0;
};
Identity Inspect(std::span<const std::byte> resource);
} // namespace hulk::eor::model
