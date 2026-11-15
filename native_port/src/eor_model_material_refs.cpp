#include "eor_model_material_refs.h"
#include <array>
#include <stdexcept>
namespace hulk::eor::model {
namespace {
uint32_t ReadHash(std::span<const std::byte> raw,size_t at,Platform platform) {
  const auto b=[&](size_t i) {return uint32_t(std::to_integer<uint8_t>(raw[i]));};
  if(platform==Platform::Xbox360)
    return (b(at)<<24)|(b(at+1)<<16)|(b(at+2)<<8)|b(at+3);
  return b(at)|(b(at+1)<<8)|(b(at+2)<<16)|(b(at+3)<<24);
}
} // namespace
std::vector<TextureReferenceCandidate> FirstSurfaceTextureCandidates(
    std::span<const std::byte> raw,const Index& index) {
  const auto model=Inspect(raw);
  if ((model.platform==Platform::Xbox360 &&
       index.byte_order()!=ByteOrder::Big) ||
      (model.platform==Platform::PcD3d9 &&
       index.byte_order()!=ByteOrder::Little))
    throw std::invalid_argument("Original EoR model/texture platform mismatch");
  if (!index.groups().contains("Textures"))
    throw std::invalid_argument("Original EoR indexed textures absent");
  std::vector<TextureReferenceCandidate> candidates;
  if (!model.surface_records || model.first_surface_name.empty())
    return candidates;
  // These are observed platform-specific FIRST SURFACE offsets, not guesses
  // that arbitrary binary words constitute a material ID. Every candidate
  // must independently resolve in the matching original texture index.
  constexpr std::array<uint32_t,7> xbox_slots{56,60,64,68,72,76,80};
  constexpr std::array<uint32_t,2> pc_slots{48,52};
  const auto visit=[&](uint32_t slot) {
    if (model.surface_data_offset>raw.size() ||
        slot>raw.size()-model.surface_data_offset ||
        raw.size()-model.surface_data_offset-slot<4) return;
    const auto hash=ReadHash(raw,model.surface_data_offset+slot,model.platform);
    if (!hash) return;
    const auto entry=index.FindHash("Textures",hash);
    if (!entry || entry->reserved || !entry->size) return;
    candidates.push_back(TextureReferenceCandidate{hash,slot,*entry});
  };
  if(model.platform==Platform::Xbox360) {
    for (auto slot:xbox_slots) visit(slot);
  } else {
    for (auto slot:pc_slots) visit(slot);
  }
  return candidates;
}
}  // namespace hulk::eor::model
