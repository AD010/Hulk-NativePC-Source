#include "eor_ui_original_dds.h"
#include "eor_native_ui_prompts.h"
#include "eor_archive.h"
#include <algorithm>
#include <stdexcept>

namespace hulk::eor::ui {
namespace {
uint32_t U32(std::span<const std::byte> b,size_t p) {
  if(p>b.size() || b.size()-p<4)
    throw std::invalid_argument("Truncated original PC UI texture field");
  return uint32_t(std::to_integer<uint8_t>(b[p])) |
      (uint32_t(std::to_integer<uint8_t>(b[p+1]))<<8) |
      (uint32_t(std::to_integer<uint8_t>(b[p+2]))<<16) |
      (uint32_t(std::to_integer<uint8_t>(b[p+3]))<<24);
}
bool Magic(std::span<const std::byte> b,size_t p,const char* s) {
  return p<=b.size() && b.size()-p>=4 &&
      b[p]==std::byte(uint8_t(s[0])) &&
      b[p+1]==std::byte(uint8_t(s[1])) &&
      b[p+2]==std::byte(uint8_t(s[2])) &&
      b[p+3]==std::byte(uint8_t(s[3]));
}
}
OriginalPcUiImage DecodePcTxflUi(
    std::span<const std::byte> b,std::string_view name) {
  if(name.empty() || name.size()>96 || !Magic(b,0,"TXFL"))
    throw std::invalid_argument("Not an original PC EoR TXFL texture");
  const size_t name_end=4u+name.size();
  if(name_end>=b.size() || b[name_end]!=std::byte{0})
    throw std::invalid_argument("Original PC UI resource name is truncated");
  for(size_t i=0;i<name.size();++i)
    if(b[4+i]!=std::byte(uint8_t(name[i])))
      throw std::invalid_argument("Original PC UI resource name mismatch");
  // Actual 2008 PC UI textures have a proprietary TXFL wrapper and a
  // standard uncompressed BGRA8 DDS after the resource metadata.
  size_t dds=SIZE_MAX;
  for(size_t p=name_end+1;p<=name_end+256 && p<b.size();++p)
    if(Magic(b,p,"DDS ")) {
      if(dds!=SIZE_MAX)
        throw std::invalid_argument("Multiple original PC UI DDS headers");
      dds=p;
    }
  if(dds==SIZE_MAX || b.size()-dds<128 ||
     U32(b,dds+4)!=124 || U32(b,dds+76)!=32 ||
     U32(b,dds+80)!=65 || U32(b,dds+84)!=0 ||
     U32(b,dds+88)!=32 || U32(b,dds+92)!=0x00ff0000 ||
     U32(b,dds+96)!=0x0000ff00 || U32(b,dds+100)!=0x000000ff ||
     U32(b,dds+104)!=0xff000000 || U32(b,dds+28)!=1)
    throw std::invalid_argument("Unsupported original PC Hulk DDS pixel format");
  const uint32_t h=U32(b,dds+12),w=U32(b,dds+16);
  if(!h || !w || h>4096 || w>4096 || uint64_t(w)*h>1048576u)
    throw std::invalid_argument("Original PC UI DDS dimensions invalid");
  const uint64_t raster_start=uint64_t(dds)+128u;
  const uint64_t raster_bytes=uint64_t(w)*h*4u;
  if(raster_start+raster_bytes!=b.size())
    throw std::invalid_argument("Original PC UI DDS raster length mismatch");
  OriginalPcUiImage image;
  image.width=w;image.height=h;image.rgba.resize(size_t(raster_bytes));
  for(size_t i=0;i<image.rgba.size();i+=4) {
    image.rgba[i]=std::to_integer<uint8_t>(b[size_t(raster_start)+i+2]);
    image.rgba[i+1]=std::to_integer<uint8_t>(b[size_t(raster_start)+i+1]);
    image.rgba[i+2]=std::to_integer<uint8_t>(b[size_t(raster_start)+i]);
    image.rgba[i+3]=std::to_integer<uint8_t>(b[size_t(raster_start)+i+3]);
  }
  return image;
}
OriginalPcUiImage LoadPcMenuArt(
    const std::filesystem::path& index_path,
    const std::filesystem::path& textures_path,std::string_view name) {
  const bool known_prompt=std::any_of(options::kBindingTokens.begin(),
      options::kBindingTokens.end(),[&](std::string_view token) {
        const auto asset=KnownPcPromptAsset(token);
        return !asset.empty() && asset==name;
      });
  if(name!="ui_menubands_da" && name!="ui_menuitems_da" && !known_prompt)
    throw std::invalid_argument("Unrecognized original Hulk PC menu asset");
  auto idx=Index::Load(index_path);
  if(idx.byte_order()!=ByteOrder::Little)
    throw std::invalid_argument("Original PC menu requires a PC LE index");
  const auto rec=idx.FindName("Textures",name);
  if(!rec || !rec->size || rec->size>1u<<20)
    throw std::invalid_argument("Original PC menu resource missing");
  const auto original=ReadResource(textures_path,*rec,1u<<20);
  return DecodePcTxflUi(original,name);
}
}
