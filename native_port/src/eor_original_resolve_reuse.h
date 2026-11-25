#pragma once
#include <array>
#include <cstdint>

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
  bool CanReuse(uint32_t source,uint32_t dest,bool full) const noexcept {
    if(!full || !source || !dest || source==dest)return false;
    const auto generation=Generation(source);
    if(!generation)return false;
    for(const auto& entry:resolves_)
      if(entry.dest==dest)return entry.source==source && entry.generation==generation;
    return false;
  }
  void Resolved(uint32_t source,uint32_t dest,bool full) noexcept {
    if(!dest)return;
    const auto generation=full && source!=dest ? Generation(source):0;
    for(auto& entry:resolves_)if(entry.dest==dest || !entry.dest) {
      entry={dest,generation?source:0u,generation};return;
    }
    resolves_={};
  }
 private:
  struct Color {uint32_t id=0;uint64_t generation=0;};
  struct Resolve {uint32_t dest=0,source=0;uint64_t generation=0;};
  uint64_t Generation(uint32_t id) const noexcept {
    if(id)for(const auto& color:colors_)if(color.id==id)return color.generation;
    return 0;
  }
  std::array<Color,16> colors_{};
  std::array<Resolve,32> resolves_{};
};
} // namespace hulk::eor::gpu
