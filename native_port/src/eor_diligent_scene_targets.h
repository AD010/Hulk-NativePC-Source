#pragma once
// Owned host-native color/depth scene images and MSAA resolve for EoR frames.
// No Xenos EDRAM aliasing; no guessed game render-state, sampler or PSO.
// Must run on the render thread of an existing DiligentCore/Vulkan device.
#include "eor_native_frame.h"
#include "eor_xdk_draw_capture.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <algorithm>
#include <set>
#include <map>
namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITextureView;
}
namespace hulk::eor::gpu {
// Explicit original EoR game color/depth format evidence must be converted
// to these host formats by the caller; never select one from surface size.
enum class NativeColorFormat : uint8_t { Unknown, Rgba8, Rgba16Float };
enum class NativeDepthFormat : uint8_t { Unknown, D24S8, D32Float, None };
// Original XDK device/screen identities must come from an independent
// USA executable memory proof. Renderer must NEVER derive them from a
// shader name, material hash or nearest active PM4 target.
struct VerifiedOriginalXdkScreen {
  uint32_t guest_device=0;
  uint32_t guest_rt0=0;
  uint32_t guest_depth=0;
  uint32_t guest_front_buffer=0;
  bool exact_guest_to_native_targets_verified=false;
  bool final_resolve_to_front_buffer_verified=false;
  bool original_tiling_has_no_host_aliases_verified=false;
};

 // A live USA Draw must still reference the SAME original RT0 and depth
 // surface that this native Vulkan render pass owns. Matching only model,
 // material and shader digests cannot detect guest target switches that
 // bypass the observed SetRenderTarget chronology.
inline bool MatchesVerifiedOriginalXdkMainDrawTargets(
    const xdk::OriginalVsMatrices& observed,
    const VerifiedOriginalXdkScreen& screen,bool depth_bound=true) noexcept {
  return screen.exact_guest_to_native_targets_verified &&
         screen.guest_device && screen.guest_rt0 && (!depth_bound || screen.guest_depth) &&
         observed.original_guest_device == screen.guest_device &&
         observed.original_rt0_guest_object == screen.guest_rt0 &&
         observed.original_depth_guest_object == (depth_bound ? screen.guest_depth : 0u) &&
         observed.original_vertex_declaration_guest_object &&
         observed.original_vs_guest_object &&
         observed.original_ps_guest_object;
}
// Each original XDK surface object owns a distinct native image. Sequential
// passes share the host extent and format only if every real Draw and Clear
// proves that contract. Intermediate Resolve destinations are separate
// images, never aliases of the currently bound color attachment.
struct OriginalNativeTargetGraph {
  std::vector<uint32_t> colors,depths,resolves;
  std::map<uint32_t,xdk::OriginalSurfaceExtent> extents;
  uint32_t first_color=0,first_depth=0,final_color=0,front_buffer=0;
  bool operator==(const OriginalNativeTargetGraph&) const=default;
  bool active() const noexcept {return !colors.empty();}
  bool HasColor(uint32_t id) const noexcept {
    return std::binary_search(colors.begin(),colors.end(),id);
  }
  bool HasDepth(uint32_t id) const noexcept {
    return !id || std::binary_search(depths.begin(),depths.end(),id);
  }
  native::Size SourceExtent(uint32_t id,native::Size fallback) const noexcept {
    const auto it=extents.find(id);
    return it==extents.end()?fallback:native::Size{it->second.width,it->second.height};
  }
  native::Size HostExtent(uint32_t id,const native::FrameGeometry& frame) const noexcept {
    const auto source=SourceExtent(id,frame.original_guest);
    return source.width==frame.original_guest.width && source.height==frame.original_guest.height ?
        frame.world : source;
  }
  bool HasResolve(uint32_t id) const noexcept {
    return std::binary_search(resolves.begin(),resolves.end(),id);
  }
};
struct VerifiedSceneFormat {
  NativeColorFormat color = NativeColorFormat::Unknown;
  NativeDepthFormat depth = NativeDepthFormat::Unknown;
  bool original_color_format_verified = false;
  bool original_depth_format_verified = false;
  VerifiedOriginalXdkScreen original_screen;
  OriginalNativeTargetGraph original_graph;
};
// Read genuine RB_COLOR0_INFO (0x2001), RB_DEPTH_INFO (0x2002) and
// RB_SURFACE_INFO (0x2000) from the SAME original game XDK Draw.
// This returns the actual GPU surface *formats* for the common supported
// RGBA8, RGBA16F and D24S8 cases. The guest frontbuffer/resolve and
// tiling ownership remain independent, mandatory XDK witnesses: never
// promote this format decoder alone to a permitted Vulkan Present.
struct OriginalSourceTargetFormat {
  VerifiedSceneFormat format;
  uint32_t samples=0;
};
inline std::optional<OriginalSourceTargetFormat>
ResolveOriginalXdkSurfaceFormats(
    const xdk::OriginalVsMatrices& guest,
    const xdk::OriginalXdkFixedGraphicsState& registers) noexcept {
  if(!guest.original_guest_device || !guest.original_rt0_guest_object ||
     registers.original_guest_device!=guest.original_guest_device)
    return std::nullopt;
  const auto surface=registers.ReadOriginalXenosRegister(0x2000u);
  const auto color=registers.ReadOriginalXenosRegister(0x2001u);
  const auto depth=registers.ReadOriginalXenosRegister(0x2002u);
  if(!surface || !color || !depth)return std::nullopt;
  const uint32_t msaa=(*surface>>16u)&3u;
  const uint32_t color_code=(*color>>16u)&15u;
  const uint32_t depth_code=(*depth>>16u)&1u;
  if(msaa>2u)return std::nullopt;
  OriginalSourceTargetFormat result{};
  // Xbox original color format 1 is GAMMA, not RGBA8 linear.
  // 7 is true FP16x4; 0 is UNORM8x4. Other original Xenos formats
  // (10:10:10:2, signed FP10, 16:16 etc.) require explicit conversion.
  if(color_code==0u)result.format.color=NativeColorFormat::Rgba8;
  else if(color_code==7u)result.format.color=NativeColorFormat::Rgba16Float;
  else return std::nullopt;
  if(guest.original_depth_guest_object) {
    // Xenos depth format 1 is 20e4 float-depth+8 stencil, not
    // interchangeable with a host 32f depth format or UNORM24.
    if(depth_code!=0u)return std::nullopt;
    result.format.depth=NativeDepthFormat::D24S8;
  } else {
    result.format.depth=NativeDepthFormat::None;
  }
  result.format.original_color_format_verified=true;
  result.format.original_depth_format_verified=true;
  result.format.original_screen.guest_device=guest.original_guest_device;
  result.format.original_screen.guest_rt0=guest.original_rt0_guest_object;
  result.format.original_screen.guest_depth=guest.original_depth_guest_object;
  result.samples=1u<<msaa;
  // Deliberately do not set exact_guest_to_native_targets_verified,
  // final_resolve_to_front_buffer_verified or
  // original_tiling_has_no_host_aliases_verified.
  return result;
}
// Build the actual ordered same-extent target graph from XDK command and
// per-Draw device state. No PM4 packet, EDRAM tile or guessed resource hash.
// Different extents/formats, depth resolves and partial resolves still
// require independent source descriptor evidence.
inline std::optional<OriginalSourceTargetFormat> PlanOriginalNativeTargets(
    const xdk::Frame& source,uint32_t width,uint32_t height) {
  if(!width || !height || !xdk::ValidateOriginalCommandTimeline(source))return {};
  OriginalSourceTargetFormat output{};
  auto& graph=output.format.original_graph;
  auto& screen=output.format.original_screen;
  std::set<uint32_t> colors,depths,resolve_destinations;
  auto register_extent=[&](uint32_t id,xdk::OriginalSurfaceExtent size) {
    if(!id || !size.width || !size.height || size.width>8192u || size.height>8192u)return false;
    const auto [it,inserted]=graph.extents.emplace(id,size);
    return inserted || it->second==size;
  };
  for(const auto& draw:source.calls) {
    if(!draw.observed_vs_matrices || !draw.observed_shader_constants ||
       !draw.observed_shader_constants->fixed_graphics_state)return {};
    const auto& state=*draw.observed_vs_matrices;
    const auto& fixed=*draw.observed_shader_constants->fixed_graphics_state;
    const auto format=ResolveOriginalXdkSurfaceFormats(state,fixed);
    const auto mask=fixed.ReadOriginalXenosRegister(0x2104u);
    const auto surface=fixed.ReadOriginalXenosRegister(0x2000u);
    if(!format || !mask || (*mask&~15u) || !surface ||
       !state.original_color_extent || !state.raster ||
       !register_extent(state.original_rt0_guest_object,*state.original_color_extent))
      return {};
    const auto extent=*state.original_color_extent;
    if((*surface&0x3fffu)<extent.width ||
       !native::MapOriginalRasterWindow(*state.raster,{extent.width,extent.height},
                                              {0,0,extent.width,extent.height}))
      return {};
    if(!output.samples) {
      output.samples=format->samples;output.format.color=format->format.color;
      output.format.depth=NativeDepthFormat::None;
      screen.guest_device=state.original_guest_device;
      graph.first_color=state.original_rt0_guest_object;
      graph.first_depth=state.original_depth_guest_object;
    }
    if(output.samples!=format->samples || output.format.color!=format->format.color ||
       screen.guest_device!=state.original_guest_device)return {};
    colors.insert(state.original_rt0_guest_object);
    if(state.original_depth_guest_object) {
      if(format->format.depth!=NativeDepthFormat::D24S8 ||
         !state.original_depth_extent || *state.original_depth_extent!=extent ||
         !register_extent(state.original_depth_guest_object,*state.original_depth_extent))return {};
      depths.insert(state.original_depth_guest_object);
      output.format.depth=NativeDepthFormat::D24S8;
    }
  }
  if(!output.samples || colors.empty() ||
     colors.size()>16 || depths.size()>16)return {};
  const auto front=source.commands.back().gpr[1];
  if(!front || colors.contains(front) || depths.contains(front))return {};
  uint32_t active_color=graph.first_color;
  std::optional<uint32_t> active_depth=graph.first_depth;
  // Do not infer a binding backwards across an explicit earlier rebind.
  for(const auto& cmd:source.commands) {
    if(cmd.draw_ordinal!=UINT32_MAX)break;
    if(cmd.kind==xdk::CommandKind::SetRenderTarget)active_color=0;
    if(cmd.kind==xdk::CommandKind::SetDepthStencilSurface)active_depth.reset();
  }
  std::set<uint32_t> initialized_color,initialized_depth,initialized_stencil,drawn;
  std::set<uint32_t> declared_resolves,completed_resolves;
  for(const auto& cmd:source.commands)
    if(cmd.kind==xdk::CommandKind::Resolve && cmd.gpr[3]!=front)
      declared_resolves.insert(cmd.gpr[3]);
  bool final_resolved=false;
  for(const auto& cmd:source.commands) {
    if(cmd.gpr[0]!=screen.guest_device)return {};
    if(cmd.draw_ordinal!=UINT32_MAX) {
      const auto& draw=source.calls[cmd.draw_ordinal];
      const auto& gpu=*draw.observed_vs_matrices;
      if(final_resolved || !active_color || !active_depth ||
         active_color!=gpu.original_rt0_guest_object ||
         *active_depth!=gpu.original_depth_guest_object ||
         !initialized_color.contains(active_color))return {};
      const auto control=draw.observed_shader_constants->fixed_graphics_state->
          ReadOriginalXenosRegister(0x2200u);
      if(!control ||
         ((*control&2u) && !initialized_depth.contains(*active_depth)) ||
         ((*control&1u) && !initialized_stencil.contains(*active_depth)))return {};
      // Read-after-write is tied to the exact sampled XDK object. A stale
      // SRV from an earlier source frame may not authorize this Draw.
      if(draw.observed_live_pixel_shader_header &&
         draw.observed_live_pixel_shader_header->pixel_sampling) {
        const auto mask=draw.observed_live_pixel_shader_header->pixel_sampling->sampled_slots;
        for(uint32_t slot=0;slot<16;++slot) {
          if(!(mask&(1u<<slot)))continue;
          const auto texture=gpu.original_texture_guest_objects[slot];
          if(declared_resolves.contains(texture)) {
            if(!completed_resolves.contains(texture) ||
               !draw.observed_shader_constants ||
               !(draw.observed_shader_constants->valid_pixel_fetch&(1u<<slot)))return {};
            const auto& fetch=draw.observed_shader_constants->pixel_fetch[slot];
            const auto extent=graph.extents.at(texture);
            if((fetch[5]>>9&3u)!=1u || (fetch[2]>>26) ||
               (fetch[2]&8191u)+1u!=extent.width || ((fetch[2]>>13)&8191u)+1u!=extent.height)
              return {};
          }
          if(colors.contains(texture) || depths.contains(texture))return {};
        }
      }
      drawn.insert(active_color);
      continue;
    }
    switch(cmd.kind) {
      case xdk::CommandKind::SetRenderTarget:
        if(final_resolved || cmd.gpr[1]!=0 || !colors.contains(cmd.gpr[2]))return {};
        active_color=cmd.gpr[2];break;
      case xdk::CommandKind::SetDepthStencilSurface:
        if(final_resolved || (cmd.gpr[1] && !depths.contains(cmd.gpr[1])))return {};
        active_depth=cmd.gpr[1];break;
      case xdk::CommandKind::ClearF: {
        if(final_resolved || !cmd.captured_clear || !active_color ||
           !graph.extents.contains(active_color))return {};
        const auto extent=graph.extents.at(active_color);
        if(active_depth && *active_depth && graph.extents.at(*active_depth)!=extent)return {};
        if(!xdk::IsOriginalFullTargetClear(*cmd.captured_clear,extent.width,extent.height) ||
           ((cmd.captured_clear->flags&0x30u) &&
             (!active_depth || !*active_depth || !graph.extents.contains(*active_depth) ||
              graph.extents.at(*active_depth)!=extent)))return {};
        if(cmd.captured_clear->flags&1u) {
          if(!active_color)return {};
          initialized_color.insert(active_color);
        }
        if(cmd.captured_clear->flags&0x30u) {
          if(!active_depth || !*active_depth)return {};
          if(cmd.captured_clear->flags&0x10u)initialized_depth.insert(*active_depth);
          if(cmd.captured_clear->flags&0x20u)initialized_stencil.insert(*active_depth);
        }
        break;
      }
      case xdk::CommandKind::Resolve: {
        const auto dest=cmd.gpr[3];
        if(final_resolved || !drawn.contains(active_color) || !dest ||
           cmd.gpr[1] || cmd.gpr[2] || cmd.gpr[4] || cmd.gpr[5] || cmd.gpr[6] ||
           colors.contains(dest) || depths.contains(dest))return {};
        const auto extent=graph.extents.at(active_color);
        if(active_depth && *active_depth && graph.extents.at(*active_depth)!=extent)return {};
        if(!register_extent(dest,extent))return {};
        if(dest==front) {
          if(extent.width!=width || extent.height!=height)return {};
          graph.final_color=active_color;graph.front_buffer=dest;final_resolved=true;
        } else {resolve_destinations.insert(dest);completed_resolves.insert(dest);}
        break;
      }
      case xdk::CommandKind::Present:
        if(!final_resolved || cmd.gpr[1]!=front)return {};
        break;
      default:return {};
    }
  }
  if(!final_resolved || resolve_destinations.size()>16)return {};
  graph.colors.assign(colors.begin(),colors.end());
  graph.depths.assign(depths.begin(),depths.end());
  graph.resolves.assign(resolve_destinations.begin(),resolve_destinations.end());
  screen.guest_rt0=graph.final_color;
  screen.guest_depth=depths.empty()?0u:*depths.begin();
  screen.guest_front_buffer=graph.front_buffer;
  screen.exact_guest_to_native_targets_verified=true;
  screen.final_resolve_to_front_buffer_verified=true;
  output.format.original_color_format_verified=true;
  output.format.original_depth_format_verified=true;
  return output;
}
inline bool MatchesVerifiedOriginalXdkDrawTargets(
    const xdk::OriginalVsMatrices& observed,const VerifiedSceneFormat& format,
    bool depth_bound) noexcept {
  const auto& graph=format.original_graph;
  if(!graph.active())return MatchesVerifiedOriginalXdkMainDrawTargets(
      observed,format.original_screen,depth_bound);
  return format.original_screen.exact_guest_to_native_targets_verified &&
         observed.original_guest_device==format.original_screen.guest_device &&
         graph.HasColor(observed.original_rt0_guest_object) &&
         graph.HasDepth(observed.original_depth_guest_object) &&
         bool(observed.original_depth_guest_object)==depth_bound &&
         observed.original_vertex_declaration_guest_object &&
         observed.original_vs_guest_object && observed.original_ps_guest_object;
}
class DiligentSceneTargets {
 public:
  DiligentSceneTargets(Diligent::IRenderDevice* device,
                       Diligent::IDeviceContext* context);
  DiligentSceneTargets(const DiligentSceneTargets&) = delete;
  DiligentSceneTargets& operator=(const DiligentSceneTargets&) = delete;
  ~DiligentSceneTargets() noexcept;
  // Allocate color, matching depth and exact-size resolved color from the
  // SAME host frame geometry. Never substitutes a guest 1280x720 surface.
  bool Prepare(const native::FrameGeometry& frame,
               VerifiedSceneFormat verified);
  bool SetDepthBound(bool bind);
  bool SetOriginalColorTarget(uint32_t guest);
  native::Size ActiveHostExtent() const noexcept;
  native::Size ActiveSourceExtent(native::Size fallback) const noexcept;
  bool SetOriginalDepthTarget(uint32_t guest);
  bool ResolveOriginalIntermediate(uint32_t guest_destination);
  Diligent::ITextureView* IntermediateColorSRV(uint32_t guest_destination) const noexcept;
  bool DepthBound() const noexcept;
  bool BeginScene(); // does not clear: the real EoR Clear/Depth ABI owns it
  // Execute original XDK ClearF on independently OWNED native RT0/depth.
  // The caller must already prove these host views own the original guest
  // surfaces, and partial guest RECTs must never become full host clears.
  bool ClearVerifiedOriginalXdk(
      const xdk::OriginalXdkClearF& source,native::Size original_guest);
  // Always call after scene draws (even 1x, to unbind the color target).
  // Only an MSAA scene performs a real same-extent Vulkan resolve.
  bool ResolveScene();
  Diligent::ITextureView* ResolvedColorSRV() const noexcept;
  void AbortScene() noexcept;
  void Reset() noexcept;
 private:
  bool BindOriginalAttachments();
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace hulk::eor::gpu
