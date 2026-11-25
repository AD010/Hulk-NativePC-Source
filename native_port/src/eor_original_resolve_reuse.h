#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace hulk::eor::gpu {
// The original replay backend is the sole writer to these source color
// attachments. A destination may reuse a full resolve only when neither
// the source generation nor the destination contents have changed.
// Fixed storage: at most 16 color and 32 resolve targets per source frame.
class OriginalResolveReuse {
 public:
  void Reset() noexcept {colors_={};resolves_={};}
  void ColorWritten(uint32_t source) noexcept {
    if(!source)return;
    for(auto& entry:colors_)if(entry.id==source || !entry.id) {
      if(entry.generation==UINT64_MAX){Reset();return;}
      entry.id=source;++entry.generation;return;
    }
    // More source targets than supported: forget reuse, never skip a copy.
    Reset();
  }
  // A verified zero RT write mask cannot modify the color image. Depth
  // and stencil writes still execute; they do not invalidate a color Resolve.
  // Unknown state is conservative, just like an actual color write.
  void DrawColorWritten(uint32_t source,std::optional<uint32_t> color_mask) noexcept {
    if(!color_mask || *color_mask!=0u)ColorWritten(source);
  }
  bool CanReuse(uint32_t source,uint32_t dest,bool full) const noexcept {
    if(!full || !source || !dest || source==dest)return false;
    const auto generation=Generation(source);
    if(!generation)return false;
    for(const auto& entry:resolves_)
      if(entry.dest==dest)return entry.source==source &&
          entry.generation==generation &&
          entry.dest_generation==Generation(dest);
    return false;
  }
  void Resolved(uint32_t source,uint32_t dest,bool full) noexcept {
    if(!dest)return;
    const auto generation=full && source!=dest ? Generation(source):0;
    // Resolve writes the destination just like a Draw/Clear. Any later
    // rendering into it must invalidate reuse, and a destination used as
    // the source of another Resolve must advance its own generation.
    ColorWritten(dest);
    const auto dest_generation=Generation(dest);
    for(auto& entry:resolves_)if(entry.dest==dest || !entry.dest) {
      entry={dest,generation?source:0u,generation,dest_generation};return;
    }
    resolves_={};
  }
 private:
  struct Color {uint32_t id=0;uint64_t generation=0;};
  struct Resolve {uint32_t dest=0,source=0;uint64_t generation=0,dest_generation=0;};
  uint64_t Generation(uint32_t id) const noexcept {
    if(id)for(const auto& color:colors_)if(color.id==id)return color.generation;
    return 0;
  }
  std::array<Color,16> colors_{};
  std::array<Resolve,32> resolves_{};
};
} // namespace hulk::eor::gpu
