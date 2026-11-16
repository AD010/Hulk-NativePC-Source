#include "eor_diligent_xenos_textures.h"
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
    case XenosTextureFormat::RGBA8:
      return space==OriginalTextureColorSpace::SRGB ?
        Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB : Diligent::TEX_FORMAT_RGBA8_UNORM;
    default:
      throw std::invalid_argument("Original EoR Xenos texture GPU format not decoded");
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
    std::span<const textures::SourceMipLayout> layouts) {
  if(!host_device || !original.hash || original.source_record.hash!=original.hash ||
     original.original_bytes.empty() ||
     color_space==OriginalTextureColorSpace::Unknown ||
     !original.pinned_usa_index)
    throw std::invalid_argument("Original EoR Xbox texture identity/color space unresolved");
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
       original.original_stream_bytes.empty())
      throw std::invalid_argument("Original TexStreams mip not resolved from Xbox index");
    // Preserve the complete original record and require source offsets in
    // the real resource, *after* its four-byte length prefix.
    (void)textures::VerifiedOriginalStreamByteRange(
        original.original_stream_bytes, mip.source_offset, 1);
  }
  const auto image=textures::PrepareExplicitOriginalMips(
      format,layouts,original.original_bytes,original.original_stream_bytes);
  if(image.mips.empty() || image.mips.size()>std::numeric_limits<Diligent::Uint32>::max())
    throw std::invalid_argument("Unverified host texture mip chain");
  const auto format_host=Convert(image.format,color_space);
  Diligent::TextureDesc desc;
  desc.Name="Original EoR verified Xbox texture";
  desc.Type=Diligent::RESOURCE_DIM_TEX_2D;
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
  host_device->CreateTexture(desc,&initial,&out.host_texture);
  if(!out.host_texture || !out.SRV())
    throw std::runtime_error("Vulkan/DiligentCore could not allocate original EoR texture");
  return out;
}
} // namespace hulk::eor::gpu
