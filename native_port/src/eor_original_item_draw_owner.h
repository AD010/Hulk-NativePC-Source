#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace hulk::eor::xdk {
inline constexpr bool IsUsaItemDrawEntry(uint32_t entry) noexcept {
  return entry==0x82667648u || entry==0x826676C0u;
}
// The virtual slot +44 is a preparation call (27 classes use a no-op).
// The same dispatcher submits the actual original mesh AFTER that call.
// Verify both complete mesh routines and the original owning dispatcher.
template<class Read>
bool VerifyUsaItemDrawCode(Read& read) {
  struct Pin {uint32_t address;size_t length;uint64_t hash;};
  constexpr std::array<Pin,3> pins{{
    {0x82642A88u,480,0x808f65319777b5e6ull},
    {0x82667648u,120,0x3dd4ad253a230bdfull},
    {0x826676C0u,112,0x849996e1c3529135ull}}};
  std::array<uint8_t,480> bytes{};
  for(const auto& p:pins) {
    if(!read(p.address,bytes.data(),p.length))return false;
    uint64_t hash=0xcbf29ce484222325ull;
    for(size_t i=0;i<p.length;++i)hash=(hash^bytes[i])*0x100000001b3ull;
    if(hash!=p.hash)return false;
  }
  return true;
}
struct UsaItemDrawRegisters {
  uint32_t entry=0,lr=0,mesh=0,device=0;
  uint32_t shader=0,item=0,context=0,material=0,node=0;
  uint32_t dispatcher_device=0,indexed=0;
};
struct UsaItemDrawOwner {
  uint32_t method=0,shader_vtable=0,item_vtable=0,material=0;
};
// Only call after the immutable original code pins have passed. Validate
// the live list node's real mesh and item identities, not just register
// values. The caller still checks its authenticated shader method scope.
template<class Read>
std::optional<UsaItemDrawOwner> ReadUsaItemDrawOwner(const UsaItemDrawRegisters& r,Read& read) {
  if(!IsUsaItemDrawEntry(r.entry) || !r.mesh || !r.device || !r.shader ||
     !r.item || !r.node || r.device!=r.dispatcher_device ||
     (r.entry==0x82667648u ? (r.lr!=0x82642C30u || r.indexed!=1u) :
                            (r.lr!=0x82642C44u || r.indexed!=0u)))return {};
  const auto word=[&](uint32_t base,uint32_t offset)->std::optional<uint32_t> {
    if(!base || (base&3u) || uint64_t(base)+offset>UINT32_MAX-3u)return {};
    std::array<uint8_t,4> b{};
    if(!read(base+offset,b.data(),b.size()))return {};
    return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];
  };
  const auto list_item=word(r.node,0),mesh=word(r.node,20);
  if(!list_item || !*list_item || mesh!=r.mesh || word(*list_item,12)!=r.item)return {};
  const auto shader_vtable=word(r.shader,0),item_vtable=word(r.item,0);
  const auto valid_vtable=[](uint32_t address){return address>=0x82000000u && address<=0x821B1FCCu && !(address&3u);};
  if(!shader_vtable || !item_vtable || !valid_vtable(*shader_vtable) || !valid_vtable(*item_vtable))return {};
  const auto method=word(*shader_vtable,44),material_getter=word(*item_vtable,48);
  const auto valid_code=[](uint32_t address){return address>=0x821B2000u && address<0x82D61523u && !(address&3u);};
  if(!method || !material_getter || !valid_code(*method) || !valid_code(*material_getter))return {};
  if(r.material && !word(r.material,0))return {};
  return UsaItemDrawOwner{*method,*shader_vtable,*item_vtable,r.material};
}
} // namespace hulk::eor::xdk
