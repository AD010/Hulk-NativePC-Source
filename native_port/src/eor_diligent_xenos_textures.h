#pragma once
// Host Vulkan-compatible DiligentCore upload of explicitly reconstructed
// original Xbox Xenos texture mip levels. No guest GPU registers/EDRAM.
// No fabricated sRGB, mip layout or shader sampler assignments.
#include "eor_original_texture_assets.h"
#include "eor_xenos_texture_mips.h"
#include "RefCntAutoPtr.hpp"
#include <cstdint>
#include <span>
namespace Diligent { struct IRenderDevice; struct ITexture; struct ITextureView; }
namespace hulk::eor::gpu {
enum class OriginalTextureColorSpace : uint8_t {
  Unknown=0, Linear, SRGB
};
struct UploadedEorTexture {
  uint32_t original_texture_hash=0;
  uint32_t original_mip_levels=0;
  Diligent::RefCntAutoPtr<Diligent::ITexture> host_texture;
  Diligent::ITextureView* SRV() const noexcept;
};
// Uses proven original texture record (via USA-pinned asset loader) and
// EXPLICIT per-mip fetch layout supplied by separately validated EoR state.
// A genuine material sampler/PSO bridge must bind SRV() to its proven slot.
UploadedEorTexture UploadExplicitOriginalTexture(
    Diligent::IRenderDevice* host_device,
    const textures::OriginalTexture& original,
    textures::XenosTextureFormat xenos_format,
    OriginalTextureColorSpace color_space,
    std::span<const textures::SourceMipLayout> proven_mip_layout);
} // namespace hulk::eor::gpu
