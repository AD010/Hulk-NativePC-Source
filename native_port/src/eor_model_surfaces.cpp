#include "eor_model_surfaces.h"
#include <algorithm>
#include <stdexcept>
#include <utility>
namespace hulk::eor::model {
namespace {
uint8_t B(std::span<const std::byte> r,size_t i) noexcept {
 return std::to_integer<uint8_t>(r[i]);
}
bool NameByte(uint8_t c) noexcept {
 return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||
        (c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'||c==' ';
}
bool FirstByte(uint8_t c) noexcept {
 return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||
        (c>='0'&&c<='9')||c=='_';
}
uint32_t U32(std::span<const std::byte> r,size_t i,Platform p) noexcept {
 if(p==Platform::Xbox360)
  return (uint32_t(B(r,i))<<24)|(uint32_t(B(r,i+1))<<16)|
         (uint32_t(B(r,i+2))<<8)|B(r,i+3);
 return B(r,i)|(uint32_t(B(r,i+1))<<8)|
        (uint32_t(B(r,i+2))<<16)|(uint32_t(B(r,i+3))<<24);
}
bool TryHeader(std::span<const std::byte> r,size_t at,size_t limit,
               Platform p,SurfaceHeader& out) {
 if(at>=limit||!FirstByte(B(r,at)))return false;
 size_t end=at;
 while(end<limit && end-at<240 && NameByte(B(r,end)))++end;
 if(end==at || end+13>limit || B(r,end)!=0)return false;
 const size_t meta=end+1,mask=meta+8;
 const auto flags=U32(r,mask,p);
 if(flags!=7 && flags<0xfffffff0u)return false;
 out.material_name=std::string(reinterpret_cast<const char*>(r.data()+at),end-at);
 out.name_offset=at;
 out.material_data_offset=meta;
 out.header_end_offset=mask+4;
 out.attribute_a=U32(r,meta,p);
 out.attribute_b=U32(r,meta+4,p);
 out.material_flags=flags;
 return true;
}
}
SurfaceDirectory ReadAllSurfaceHeaders(std::span<const std::byte> raw) {
 const auto id=Inspect(raw);
 SurfaceDirectory result;
 result.platform=id.platform;result.original_model_name=id.model_name;
 result.model_kind=id.model_kind;
 if(!id.surface_records)return result;
 if(id.surface_records>1024 || id.first_surface_name.size()+1>id.surface_data_offset)
  throw std::invalid_argument("Invalid EoR original surface directory");
 const size_t first=id.surface_data_offset-id.first_surface_name.size()-1;
 result.first_surface_offset=first;
 const size_t budget=std::max<size_t>(8192u,size_t(id.surface_records)*240u+4096u);
 const size_t limit=std::min(raw.size(),budget);
 result.surfaces.reserve(id.surface_records);
 size_t from=first;
 for(uint32_t ordinal=0;ordinal<id.surface_records;++ordinal) {
  SurfaceHeader surface;
  bool found=false;
  for(size_t at=from;at<limit;++at) {
   if(TryHeader(raw,at,limit,id.platform,surface)) {
    found=true;from=surface.header_end_offset;break;
   }
  }
  if(!found)throw std::invalid_argument("Original EoR surface header truncated");
  if(ordinal==0 && (surface.name_offset!=first ||
     surface.material_name!=id.first_surface_name))
   throw std::invalid_argument("Original EoR first surface identity mismatch");
  surface.ordinal=ordinal;
  result.last_surface_header_end=surface.header_end_offset;
  result.surfaces.push_back(std::move(surface));
 }
 // NOTE: last_surface_header_end is NOT the mesh vertex/index stream start.
 return result;
}
}
