#include "eor_diligent_xenos_textures.h"
#include "eor_original_texture_mip_ranges.h"
#include "RenderDevice.h"
#include "Texture.h"
#include "TextureView.h"
#include <limits>
#include <stdexcept>
#include <vector>
namespace hulk::eor::gpu {
namespace {
Diligent::TEXTURE_FORMAT Convert(textures::XenosTextureFormat fmt,
                                  OriginalTextureColorSpace space) {
  using textures::XenosTextureFormat;
  switch(fmt) {
    case XenosTextureFormat::BC1:
      return space==OriginalTextureColorSpace::SRGB ?
        Diligent::TEX_FORMAT_BC1_UNORM_SRGB : Diligent::TEX_FORMAT_BC1_UNORM;
    case XenosTextureFormat::BC2:
      return space==OriginalTextureColorSpace::SRGB ?
        Diligent::TEX_FORMAT_BC2_UNORM_SRGB : Diligent::TEX_FORMAT_BC2_UNORM;
    case XenosTextureFormat::BC3:
      return space==OriginalTextureColorSpace::SRGB ?
        Diligent::TEX_FORMAT_BC3_UNORM_SRGB : Diligent::TEX_FORMAT_BC3_UNORM;
    case XenosTextureFormat::BC4:
      if(space!=OriginalTextureColorSpace::Linear)
        throw std::invalid_argument("Original BC4 single-channel texture cannot be sRGB");
      return Diligent::TEX_FORMAT_BC4_UNORM;
    case XenosTextureFormat::BC5:
      if(space!=OriginalTextureColorSpace::Linear)
        throw std::invalid_argument("Original BC5 normal texture cannot be sRGB");
      return Diligent::TEX_FORMAT_BC5_UNORM;
    case XenosTextureFormat::RGBA16F:
      if(space!=OriginalTextureColorSpace::Linear)
        throw std::invalid_argument("Original HDR RGBA16F lightmap cannot be sRGB");
      return Diligent::TEX_FORMAT_RGBA16_FLOAT;
    case XenosTextureFormat::RGBA8:
      // The authenticated USA 0x06 descriptor has fetch word 3 = 0xC14.
      // Its four swizzle selectors (bits 1..12) are B,G,R,A.
      return space==OriginalTextureColorSpace::SRGB ?
        Diligent::TEX_FORMAT_BGRA8_UNORM_SRGB : Diligent::TEX_FORMAT_BGRA8_UNORM;
    default:
      throw std::invalid_argument("Original EoR Xenos texture GPU format not decoded");
  }
}
// Exact original LFXT allocation FAMILY does not specify a complete Xenos
// GPU-format enum; the caller must independently verify BC subformat,
// channel semantics, endian and sRGB with the live USA fetch declaration.
textures::OriginalLfxtStorageFamily Family(textures::XenosTextureFormat fmt) {
  using textures::XenosTextureFormat;
  using textures::OriginalLfxtStorageFamily;
  switch(fmt) {
    case XenosTextureFormat::BC1:
    case XenosTextureFormat::BC4:return OriginalLfxtStorageFamily::BC_8ByteBlock;
    case XenosTextureFormat::BC2:
    case XenosTextureFormat::BC3:
    case XenosTextureFormat::BC5:return OriginalLfxtStorageFamily::BC_16ByteBlock;
    case XenosTextureFormat::RGBA16F:return OriginalLfxtStorageFamily::Texel_8Byte;
    case XenosTextureFormat::RGBA8:return OriginalLfxtStorageFamily::Texel_4Byte;
    default: return OriginalLfxtStorageFamily::Unknown;
  }
}
}
Diligent::ITextureView* UploadedEorTexture::SRV() const noexcept {
  return host_texture ?
    host_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}
UploadedEorTexture UploadExplicitOriginalTexture(
    Diligent::IRenderDevice* host_device,
    const textures::OriginalTexture& original,
    textures::XenosTextureFormat format,
    OriginalTextureColorSpace color_space,
    std::span<const textures::SourceMipLayout> layouts,
    OriginalTextureViewKind view_kind) {
  if((view_kind!=OriginalTextureViewKind::Native2D &&
      view_kind!=OriginalTextureViewKind::NativeX2DArray) ||
     !host_device || !original.hash || original.source_record.hash!=original.hash ||
     original.original_bytes.empty() ||
     color_space==OriginalTextureColorSpace::Unknown ||
     !original.pinned_usa_index)
    throw std::invalid_argument("Original EoR Xbox texture identity/color space unresolved");
  // Require a source-authentic complete high-to-low mip chain, including
  // resident lower levels from the original Textures.ARC. A valid BC
  // allocation size is NOT evidence of BC2 vs BC3 vs BC5 subformat.
  if(!original.original_mip_plan ||
     original.original_mip_plan->family!=Family(format) ||
     original.original_mip_plan->levels.size()!=layouts.size())
    throw std::invalid_argument("Original Xbox texture format family/mip chain incomplete");
  // Direct original LFXT and TexStreams byte inspection confirmed their
  // genuine structured 52-byte GPU descriptor TRAILERS. Only the matching
  // pinned-USA image layouts may authorize host upload; the shader fetch
  // binding/PSO still requires separate real Draw provenance.
  if(!original.original_mip_plan->absolute_source_offsets_proven)
    throw std::invalid_argument("Original Xbox mip GPU byte starts not source-proven");
  if(!original.verified_gpu_mips ||
     format!=original.verified_gpu_mips->format ||
     layouts.size()!=original.verified_gpu_mips->layouts.size())
    throw std::invalid_argument("Requested GPU mip layout disagrees with source-proven USA Xenos fetch");
  for(size_t i=0;i<layouts.size();++i) {
    const auto& proof=original.verified_gpu_mips->layouts[i];
    const auto& candidate=layouts[i];
    if(candidate.from_stream_archive!=proof.from_stream_archive ||
       candidate.source_offset!=proof.source_offset ||
       candidate.width!=proof.width || candidate.height!=proof.height ||
       candidate.storage_pitch_blocks!=proof.storage_pitch_blocks ||
       candidate.storage_height_blocks!=proof.storage_height_blocks ||
       candidate.source_x_blocks!=proof.source_x_blocks ||
       candidate.source_y_blocks!=proof.source_y_blocks ||
       candidate.endian!=proof.endian || candidate.tiling!=proof.tiling)
      throw std::invalid_argument("Unverified original EoR mip tile offsets/endian");
  }
  for(size_t i=0;i<layouts.size();++i) {
    const auto& actual=layouts[i];
    const auto& expected=original.original_mip_plan->levels[i];
    if(actual.width!=expected.width ||
       actual.height!=expected.height ||
       actual.from_stream_archive!=expected.from_stream_archive)
      throw std::invalid_argument("Original LFXT mip dimensions/source do not match real USA descriptor");
  }
  for(const auto& mip:layouts) {
    if(!mip.from_stream_archive) {
      // For the independent USA-native Vulkan path, offsets in Textures.ARC
      // are full-record offsets and MUST point to a validated LFXT suffix.
      if(!original.original_inline_section ||
         mip.source_offset<original.original_inline_section->inline_data_offset ||
         mip.source_offset>=original.original_bytes.size())
        throw std::invalid_argument("Original LFXT mip points into undocumented header");
      continue;
    }
    if(!original.original_stream ||
       original.original_stream->original_texture_hash!=original.hash ||
       original.original_stream_bytes.empty() ||
       !original.original_stream_table ||
       original.original_stream_bytes.size()!=
           original.original_stream->indexed_stream.size)
      throw std::invalid_argument("Original TexStreams mip not resolved from Xbox index");
    // Preserve the complete original record and require source offsets in
    // the real resource, *after* its four-byte length prefix.
    (void)textures::VerifiedOriginalStreamByteRange(
        original.original_stream_bytes, mip.source_offset, 1);
  }
  if(!original.original_inline_section)
    throw std::invalid_argument("Original LFXT GPU mip has no verified inline allocation");
  textures::ValidateOriginalMipPayloadRanges(
      original.original_bytes,original.original_stream_bytes,
      *original.original_inline_section,*original.original_mip_plan,
      format,layouts);
  const auto image=textures::PrepareExplicitOriginalMips(
      format,layouts,original.original_bytes,original.original_stream_bytes);
  if(image.mips.empty() || image.mips.size()>std::numeric_limits<Diligent::Uint32>::max())
    throw std::invalid_argument("Unverified host texture mip chain");
  const auto format_host=Convert(image.format,color_space);
  Diligent::TextureDesc desc;
  desc.Name="Original EoR verified Xbox texture";
  // A NativeX shader expects Texture2DArray for 2D fetches (including one
  // layer). A bare Texture2D view is incompatible with that SPIR-V image
  // type. Only the caller's verified live fetch/PSO may select array mode.
  desc.Type=view_kind==OriginalTextureViewKind::NativeX2DArray ?
      Diligent::RESOURCE_DIM_TEX_2D_ARRAY : Diligent::RESOURCE_DIM_TEX_2D;
  desc.Format=format_host;
  desc.Width=image.mips.front().width;
  desc.Height=image.mips.front().height;
  desc.MipLevels=static_cast<Diligent::Uint32>(image.mips.size());
  desc.ArraySize=1;
  desc.SampleCount=1;
  desc.Usage=Diligent::USAGE_DEFAULT;
  desc.BindFlags=Diligent::BIND_SHADER_RESOURCE;
  std::vector<Diligent::TextureSubResData> data;
  data.reserve(image.mips.size());
  for(const auto& level:image.mips) {
    Diligent::TextureSubResData part;
    part.pData=level.blocks.data();
    part.Stride=level.row_pitch_bytes;
    data.push_back(part);
  }
  Diligent::TextureData initial;
  initial.pSubResources=data.data();
  initial.NumSubresources=static_cast<Diligent::Uint32>(data.size());
  UploadedEorTexture out;
  out.original_texture_hash=original.hash;
  out.original_mip_levels=initial.NumSubresources;
  out.view_kind=view_kind;
  host_device->CreateTexture(desc,&initial,&out.host_texture);
  if(!out.host_texture || !out.SRV())
    throw std::runtime_error("Vulkan/DiligentCore could not allocate original EoR texture");
  return out;
}
UploadedEorTexture UploadVerifiedOriginalTexture(
    Diligent::IRenderDevice* host,
    const textures::OriginalTexture& original,
    OriginalTextureColorSpace color_space,
    OriginalTextureViewKind view_kind) {
  if(!original.verified_gpu_mips)
    throw std::invalid_argument("No authentic original Xbox GPU texture layout");
  return UploadExplicitOriginalTexture(host,original,
      original.verified_gpu_mips->format,color_space,
      original.verified_gpu_mips->layouts,view_kind);
}
} // namespace hulk::eor::gpu
