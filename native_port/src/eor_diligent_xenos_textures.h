#pragma once
// Host Vulkan-compatible DiligentCore upload of explicitly reconstructed
// original Xbox Xenos texture mip levels. No guest GPU registers/EDRAM.
// No fabricated sRGB, mip layout or shader sampler assignments.
#include "eor_original_texture_assets.h"
#include "eor_xenos_texture_mips.h"
#include "Texture.h"
#include "RefCntAutoPtr.hpp"
#include <cstdint>
#include <array>
#include <memory>
#include <mutex>
#include "TextureView.h"
#include <span>
namespace Diligent { struct IRenderDevice; struct ITexture; struct ITextureView; }
namespace hulk::eor::gpu {
enum class OriginalTextureColorSpace : uint8_t {
  Unknown=0, Linear, SRGB
};
// The existing independent EoR renderer uses a 2D host SRV. NativeX D140
// translates 1D/2D Xenos fetches as array images, even for a single layer.
// This is an explicit PSO/fetch-proven choice, not guessed from texture size.
enum class OriginalTextureViewKind : uint8_t {
  Native2D=0, NativeX2DArray=1, Native1D=2, NativeCube=3
};
struct UploadedEorTexture {
  uint32_t original_texture_hash=0;
  uint32_t original_mip_levels=0;
  OriginalTextureViewKind view_kind=OriginalTextureViewKind::Native2D;
  Diligent::RefCntAutoPtr<Diligent::ITexture> host_texture;
  Diligent::ITextureView* SRV() const noexcept;
  // Original source texture is immutable. Retain each exact mip-suffix SRV
  // with its image instead of recreating a Vulkan image view per Draw.
  struct MipViewCache {
    std::mutex mutex;
    std::array<Diligent::RefCntAutoPtr<Diligent::ITextureView>,16> views{};
  };
  std::shared_ptr<MipViewCache> mip_views=std::make_shared<MipViewCache>();
  Diligent::ITextureView* MipSuffixSRV(uint32_t base,uint32_t count) const;
};
// Uses proven original texture record (via USA-pinned asset loader) and
// EXPLICIT per-mip fetch layout supplied by separately validated EoR state.
// A genuine material sampler/PSO bridge must bind SRV() to its proven slot.
UploadedEorTexture UploadExplicitOriginalTexture(
    Diligent::IRenderDevice* host_device,
    const textures::OriginalTexture& original,
    textures::XenosTextureFormat xenos_format,
    OriginalTextureColorSpace color_space,
    std::span<const textures::SourceMipLayout> proven_mip_layout,
    OriginalTextureViewKind proven_view_kind=OriginalTextureViewKind::Native2D);
// Format/mip locations come from original verified Xbox resources; the caller
// still supplies actual material/color-space information (not inferred).
UploadedEorTexture UploadVerifiedOriginalTexture(
    Diligent::IRenderDevice* host_device,
    const textures::OriginalTexture& original,
    OriginalTextureColorSpace proven_color_space,
    OriginalTextureViewKind proven_view_kind=OriginalTextureViewKind::Native2D);
} // namespace hulk::eor::gpu
