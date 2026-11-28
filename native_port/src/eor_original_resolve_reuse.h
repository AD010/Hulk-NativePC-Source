#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace hulk::eor::gpu {
// A destination may reuse a full resolve only while both image contents
// remain unchanged. A unique write epoch prevents an evicted identity
// from resurrecting a cached resolve when it returns to the table.
class OriginalResolveReuse {
 public:
  void Reset() noexcept {colors_={};resolves_={};epoch_=0;next_resolve_=0;}
  void ColorWritten(uint32_t source) noexcept {
    if(!source)return;
    if(epoch_==UINT64_MAX)Reset();
    Color* oldest=&colors_.front();
    for(auto& entry:colors_){
      if(entry.id==source){entry.generation=++epoch_;return;}
      if(entry.generation<oldest->generation)oldest=&entry;
    }
    *oldest={source,++epoch_};
  }
  // A verified zero RT write mask cannot modify the color image. Depth
  // and stencil writes still execute; they do not invalidate a color Resolve.
  void DrawColorWritten(uint32_t source,std::optional<uint32_t> color_mask) noexcept {
    if(!color_mask || *color_mask!=0u)ColorWritten(source);
  }
  bool CanReuse(uint32_t source,uint32_t dest,bool full) const noexcept {
    if(!full || !source || !dest || source==dest)return false;
    const auto generation=Generation(source),dest_generation=Generation(dest);
    if(!generation||!dest_generation)return false;
    for(const auto& entry:resolves_)
      if(entry.dest==dest)return entry.source==source &&
          entry.generation==generation && entry.dest_generation==dest_generation;
    return false;
  }
  void Resolved(uint32_t source,uint32_t dest,bool full) noexcept {
    if(!dest)return;
    const auto generation=full && source!=dest ? Generation(source):0;
    ColorWritten(dest);
    const auto dest_generation=Generation(dest);
    // The oldest source may have been evicted by the destination write.
    if(!generation||!dest_generation||Generation(source)!=generation)return;
    Resolve* free=nullptr;
    for(auto& entry:resolves_){
      if(entry.dest==dest){entry={dest,source,generation,dest_generation};return;}
      if(!entry.dest&&!free)free=&entry;
    }
    if(!free){free=&resolves_[next_resolve_];next_resolve_=(next_resolve_+1)%resolves_.size();}
    *free={dest,source,generation,dest_generation};
  }
 private:
  struct Color {uint32_t id=0;uint64_t generation=0;};
  struct Resolve {uint32_t dest=0,source=0;uint64_t generation=0,dest_generation=0;};
  uint64_t Generation(uint32_t id) const noexcept {
    if(id)for(const auto& color:colors_)if(color.id==id)return color.generation;
    return 0;
  }
  std::array<Color,64> colors_{};
  std::array<Resolve,64> resolves_{};
  uint64_t epoch_=0;
  unsigned next_resolve_=0;
};
} // namespace hulk::eor::gpu
