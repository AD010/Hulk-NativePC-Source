#include "eor_model_material_refs.h"
#include "eor_model_binary.h"
#include <stdexcept>
namespace hulk::eor::model {
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
  const auto visit=[&](uint32_t slot) {
    if (model.surface_data_offset>raw.size() ||
        slot>raw.size()-model.surface_data_offset ||
        raw.size()-model.surface_data_offset-slot<4) return;
    const auto hash=binary::U32(raw,model.surface_data_offset+slot,model.platform);
    if (!hash) return;
    const auto entry=index.FindHash("Textures",hash);
    if (!entry || entry->reserved || !entry->size) return;
    candidates.push_back(TextureReferenceCandidate{hash,slot,*entry});
  };
  for(auto slot:binary::TextureSlots(model.platform))visit(slot);
  return candidates;
}
}  // namespace hulk::eor::model
