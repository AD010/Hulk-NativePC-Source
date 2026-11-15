#include "eor_model_surface_materials.h"
#include <array>
#include <stdexcept>
#include <utility>
namespace hulk::eor::model {
namespace {
uint32_t Hash(std::span<const std::byte> data,size_t i,Platform p) noexcept {
 const auto b=[&](size_t x){return uint32_t(std::to_integer<uint8_t>(data[x]));};
 if(p==Platform::Xbox360)
  return (b(i)<<24)|(b(i+1)<<16)|(b(i+2)<<8)|b(i+3);
 return b(i)|(b(i+1)<<8)|(b(i+2)<<16)|(b(i+3)<<24);
}
}
std::vector<SurfaceMaterials> AllSurfaceMaterialCandidates(
 std::span<const std::byte> bytes,const Index& index) {
 const auto model=ReadAllSurfaceHeaders(bytes);
 if((model.platform==Platform::Xbox360 && index.byte_order()!=ByteOrder::Big) ||
    (model.platform==Platform::PcD3d9 && index.byte_order()!=ByteOrder::Little))
  throw std::invalid_argument("Original EoR model/texture index platform mismatch");
 if(!index.groups().contains("Textures"))
  throw std::invalid_argument("Original indexed textures absent");
 constexpr std::array<uint32_t,7> xbox{56,60,64,68,72,76,80};
 constexpr std::array<uint32_t,2> pc{48,52};
 std::vector<SurfaceMaterials> result;
 result.reserve(model.surfaces.size());
 for(size_t i=0;i<model.surfaces.size();++i) {
  const auto& s=model.surfaces[i];
  const size_t next=(i+1<model.surfaces.size())
                     ?model.surfaces[i+1].name_offset:bytes.size();
  SurfaceMaterials rec{s.ordinal,s.material_name,s.material_flags,
                       s.material_data_offset,{}};
  const auto visit=[&](uint32_t slot) {
   const size_t base=s.material_data_offset;
   // Never let a candidate from one material read into the next material.
   if(base>next || slot>next-base || next-base-slot<4)return;
   auto hash=Hash(bytes,base+slot,model.platform);
   if(!hash)return;
   auto hit=index.FindHash("Textures",hash);
   if(!hit || !hit->size || hit->reserved)return;
   rec.indexed_texture_references.push_back(TextureReferenceCandidate{hash,slot,*hit});
  };
  if(model.platform==Platform::Xbox360)for(auto slot:xbox)visit(slot);
  else for(auto slot:pc)visit(slot);
  result.push_back(std::move(rec));
 }
 return result;
}
}
