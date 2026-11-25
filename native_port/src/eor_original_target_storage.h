#pragma once
#include <cstdint>

namespace hulk::eor::gpu {
// Compare source-owned offscreen allocations independently of the rotating
// Present destination. The front buffer is NOT allocated by SceneTargets:
// the final color image is composited to the physical host swapchain.
template<class Graph>
bool SameOriginalTargetStorage(const Graph& a,const Graph& b) noexcept {
  if(!a.active() || !b.active())return a==b;
  const auto valid=[](const Graph& g) {
    const auto id=g.front_buffer;
    return id && !g.HasColor(id) && !g.HasDepth(id) && !g.HasResolve(id) &&
        g.extents.contains(id) && g.color_formats.contains(id) &&
        g.sample_counts.contains(id) && g.sample_counts.at(id)==1u;
  };
  if(!valid(a) || !valid(b))return false;
  if(a.colors!=b.colors || a.depths!=b.depths || a.resolves!=b.resolves ||
     a.partial_msaa_sources!=b.partial_msaa_sources ||
     a.first_color!=b.first_color || a.first_depth!=b.first_depth ||
     a.final_color!=b.final_color)return false;
  const auto same=[&](const auto& x,const auto& y) {
    if(x.size()!=y.size() || x.at(a.front_buffer)!=y.at(b.front_buffer))return false;
    for(const auto& [id,value]:x) {
      if(id==a.front_buffer)continue;
      if(id==b.front_buffer)return false;
      const auto it=y.find(id);
      if(it==y.end() || it->second!=value)return false;
    }
    return true;
  };
  return same(a.extents,b.extents) && same(a.color_formats,b.color_formats) &&
         same(a.sample_counts,b.sample_counts);
}
} // namespace hulk::eor::gpu
