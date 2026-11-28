#pragma once
#include <cstdint>
namespace hulk::eor::gpu {
enum class NativeResourceUpdate : uint8_t { Reuse, ReplaceShaderPack, Recreate };
constexpr NativeResourceUpdate SelectNativeResourceUpdate(bool exists,
    bool same_device_archive, bool same_shader_pack) noexcept {
  if(!exists || !same_device_archive)return NativeResourceUpdate::Recreate;
  return same_shader_pack ? NativeResourceUpdate::Reuse : NativeResourceUpdate::ReplaceShaderPack;
}
} // namespace hulk::eor::gpu
