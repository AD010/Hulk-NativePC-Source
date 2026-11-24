#include "eor_diligent_scene_targets.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Texture.h"
#include "TextureView.h"
#include "RefCntAutoPtr.hpp"
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
namespace hulk::eor::gpu {
namespace {
Diligent::TEXTURE_FORMAT Color(NativeColorFormat format) {
  switch (format) {
    case NativeColorFormat::Rgba8: return Diligent::TEX_FORMAT_RGBA8_UNORM;
    case NativeColorFormat::Rgba16Float: return Diligent::TEX_FORMAT_RGBA16_FLOAT;
    default: return Diligent::TEX_FORMAT_UNKNOWN;
  }
}
Diligent::TEXTURE_FORMAT Depth(NativeDepthFormat format) {
  switch (format) {
    case NativeDepthFormat::D24S8: return Diligent::TEX_FORMAT_D24_UNORM_S8_UINT;
    case NativeDepthFormat::D32Float: return Diligent::TEX_FORMAT_D32_FLOAT;
    default: return Diligent::TEX_FORMAT_UNKNOWN;
  }
}
bool Same(native::Size a,native::Size b) { return a.width==b.width && a.height==b.height; }
}
struct DiligentSceneTargets::State {
  State(Diligent::IRenderDevice* d,Diligent::IDeviceContext* c):device(d),context(c){}
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
  Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
  Diligent::RefCntAutoPtr<Diligent::ITexture> color,depth,resolved;
  std::map<uint32_t,Diligent::RefCntAutoPtr<Diligent::ITexture>> colors,depths,intermediates;
  std::set<uint32_t> completed_intermediates;
  uint32_t active_color=0,active_depth=0;
  native::Size physical{},original{};
  VerifiedSceneFormat formats{};
  uint32_t samples=0;
  bool prepared=false, bound=false, depth_bound=false, ready=false;
};
DiligentSceneTargets::DiligentSceneTargets(Diligent::IRenderDevice* d,Diligent::IDeviceContext* c)
    :state_(d && c ? std::make_unique<State>(d,c) : nullptr) {
  if (!state_) throw std::invalid_argument("Host EoR Vulkan device/context absent");
}
DiligentSceneTargets::~DiligentSceneTargets() noexcept {AbortScene();}
void DiligentSceneTargets::AbortScene() noexcept {
  if (!state_) return;
  if (state_->bound) {
    try {state_->context->SetRenderTargets(0,nullptr,nullptr,
               Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);} catch (...) {}
    state_->bound=false;
  }
  state_->ready=false;
  state_->prepared=false;
  state_->completed_intermediates.clear();
}
void DiligentSceneTargets::Reset() noexcept {
  if(!state_)return;
  AbortScene();
  state_->resolved.Release();state_->depth.Release();state_->color.Release();
  state_->colors.clear();state_->depths.clear();state_->intermediates.clear();
  state_->active_color=state_->active_depth=0;
  state_->physical={};state_->original={};state_->formats={};state_->samples=0;
}
bool DiligentSceneTargets::Prepare(const native::FrameGeometry& frame,
                                  VerifiedSceneFormat verified) {
  // A nested Prepare must not invalidate an active render pass. In
  // particular ResolveScene still needs the original prepared state.
  if (state_->bound) return false;
  // Never allow BeginScene after Prepare fails or returns an old allocation.
  state_->prepared=false;
  state_->ready=false;
  state_->completed_intermediates.clear();
  const auto& graph=verified.original_graph;
  if(graph.active() &&
     (!graph.HasColor(graph.first_color) || !graph.HasColor(graph.final_color) ||
      !graph.HasDepth(graph.first_depth) || graph.final_color!=verified.original_screen.guest_rt0 ||
      graph.front_buffer!=verified.original_screen.guest_front_buffer ||
      graph.colors.size()>16 || graph.depths.size()>16 || graph.resolves.size()>16))
    return false;
  if(!frame.direct_engine_path_ready || !verified.original_color_format_verified ||
     !verified.original_depth_format_verified ||
     Color(verified.color)==Diligent::TEX_FORMAT_UNKNOWN ||
     (verified.depth==NativeDepthFormat::None ?
          (!verified.original_screen.exact_guest_to_native_targets_verified || !verified.original_screen.guest_device ||
       !verified.original_screen.guest_rt0 || verified.original_screen.guest_depth) :
          Depth(verified.depth)==Diligent::TEX_FORMAT_UNKNOWN) ||
     !frame.world.width || !frame.world.height || frame.world.width>16384 ||
     frame.world.height>16384 || !frame.backbuffer.width || !frame.backbuffer.height ||
     frame.world.width>frame.backbuffer.width || frame.world.height>frame.backbuffer.height ||
     (frame.samples!=1 && frame.samples!=2 && frame.samples!=4 && frame.samples!=8) ||
     frame.composite.width!=frame.world.width ||
     frame.composite.height!=frame.world.height ||
     uint64_t(frame.composite.left)+frame.world.width>frame.backbuffer.width ||
     uint64_t(frame.composite.top)+frame.world.height>frame.backbuffer.height)
    return false;
  if(Same(state_->physical,frame.world) && Same(state_->original,frame.original_guest) && state_->samples==frame.samples &&
     state_->formats.color==verified.color && state_->formats.depth==verified.depth &&
     state_->color && (state_->depth || verified.depth==NativeDepthFormat::None ||
                      graph.active()) && state_->resolved &&
     state_->formats.original_graph==graph) {
    // Native attachment storage is reusable at the same extent/format,
    // but the verified Xbox RT0/depth/front-buffer *owner* can change
    // after a menu transition, loading screen or resource recreation.
    // Never retain last Present's source identity with the new frame.
    state_->formats=verified;
    if(graph.active()) {
      state_->active_color=graph.first_color;state_->active_depth=graph.first_depth;
      state_->color=state_->colors.at(graph.first_color);
      if(graph.first_depth)state_->depth=state_->depths.at(graph.first_depth);
      else state_->depth.Release();
    }
    state_->prepared=true;
    return true;
  }
  // Transactional attachment allocation: a failure keeps the preceding
  // complete target set, rather than presenting an empty/half-size image.
  Diligent::RefCntAutoPtr<Diligent::ITexture> new_color,new_depth,new_resolved;
  decltype(state_->colors) new_colors,new_depths,new_intermediates;
  Diligent::TextureDesc desc;
  desc.Name="EoR verified host color MSAA";
  desc.Type=Diligent::RESOURCE_DIM_TEX_2D;
  desc.Width=frame.world.width;desc.Height=frame.world.height;
  desc.MipLevels=1;desc.ArraySize=1;
  desc.SampleCount=frame.samples;
  desc.Format=Color(verified.color);
  desc.Usage=Diligent::USAGE_DEFAULT;
  desc.BindFlags=frame.samples==1 ?
     Diligent::BIND_FLAGS(Diligent::BIND_RENDER_TARGET|Diligent::BIND_SHADER_RESOURCE) :
     Diligent::BIND_RENDER_TARGET;
  auto set_extent=[&](uint32_t id) {
    const auto size=graph.HostExtent(id,frame);
    desc.Width=size.width;desc.Height=size.height;
  };
  try {
    if(graph.active() && !Same(graph.HostExtent(graph.final_color,frame),frame.world))return false;
    state_->device->CreateTexture(desc,nullptr,&new_color);
    if(!new_color || !new_color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET))return false;
    if(graph.active()) {
      new_colors.emplace(graph.final_color,new_color);
      for(uint32_t guest:graph.colors) {
        if(guest==graph.final_color)continue;
        Diligent::RefCntAutoPtr<Diligent::ITexture> color;
        desc.Name="EoR independent sequential host color";
        set_extent(guest);
        state_->device->CreateTexture(desc,nullptr,&color);
        if(!color || !color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET))return false;
        if(!new_colors.emplace(guest,std::move(color)).second)return false;
      }
    }
    if(verified.depth!=NativeDepthFormat::None) {
      desc.Name="EoR verified host depth";
      if(graph.active()) {
        if(graph.depths.empty())return false;
        set_extent(graph.depths.front());
      }
      desc.Format=Depth(verified.depth);
      desc.BindFlags=Diligent::BIND_DEPTH_STENCIL;
      state_->device->CreateTexture(desc,nullptr,&new_depth);
      if(!new_depth || !new_depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL))return false;
      if(graph.active()) {
        if(graph.depths.empty())return false;
        new_depths.emplace(graph.depths.front(),new_depth);
        for(size_t i=1;i<graph.depths.size();++i) {
          Diligent::RefCntAutoPtr<Diligent::ITexture> depth;
          desc.Name="EoR independent sequential host depth";
          set_extent(graph.depths[i]);
          state_->device->CreateTexture(desc,nullptr,&depth);
          if(!depth || !depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL))return false;
          if(!new_depths.emplace(graph.depths[i],std::move(depth)).second)return false;
        }
      }
    }
    if(frame.samples!=1) {
      desc.Name="EoR resolved host scene color";
      desc.Width=frame.world.width;desc.Height=frame.world.height;
      desc.SampleCount=1;desc.Format=Color(verified.color);
      desc.BindFlags=Diligent::BIND_FLAGS(Diligent::BIND_RENDER_TARGET|Diligent::BIND_SHADER_RESOURCE);
      state_->device->CreateTexture(desc,nullptr,&new_resolved);
      if(!new_resolved || !new_resolved->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE))return false;
    } else {
      new_resolved=new_color;
      if(!new_resolved->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE))return false;
    }
    if(graph.active()) {
      desc.Name="EoR original intermediate Resolve image";
      desc.SampleCount=1;desc.Format=Color(verified.color);
      desc.BindFlags=Diligent::BIND_SHADER_RESOURCE;
      for(uint32_t guest:graph.resolves) {
        set_extent(guest);
        Diligent::RefCntAutoPtr<Diligent::ITexture> target;
        state_->device->CreateTexture(desc,nullptr,&target);
        if(!target || !target->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE) ||
           !new_intermediates.emplace(guest,std::move(target)).second)return false;
      }
    }
  } catch (...) {return false;}
  state_->color=std::move(new_color);
  state_->depth=std::move(new_depth);
  state_->resolved=std::move(new_resolved);
  state_->colors=std::move(new_colors);
  state_->depths=std::move(new_depths);
  state_->intermediates=std::move(new_intermediates);
  state_->active_color=state_->active_depth=0;
  if(graph.active()) {
    state_->active_color=graph.first_color;state_->active_depth=graph.first_depth;
    state_->color=state_->colors.at(graph.first_color);
    if(graph.first_depth)state_->depth=state_->depths.at(graph.first_depth);
    else state_->depth.Release();
  }
  state_->physical=frame.world;state_->original=frame.original_guest;
  state_->formats=verified;
  state_->samples=frame.samples;
  state_->prepared=true;
  return true;
}
bool DiligentSceneTargets::BeginScene() {
  if (!state_ || !state_->prepared || state_->bound || !state_->color || !state_->resolved) return false;
  state_->ready=false;
  try {
    auto* rtv=state_->color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    state_->depth_bound=state_->formats.original_graph.active() ?
        state_->active_depth!=0 : state_->formats.depth!=NativeDepthFormat::None;
    auto* dsv=state_->depth ? state_->depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL) : nullptr;
    if(!rtv || (state_->depth_bound && !dsv))return false;
    // A driver may bind the target and then throw (device loss/resize).
    // Record ownership first so AbortScene always attempts to detach it.
    state_->bound=true;
    state_->context->SetRenderTargets(1,&rtv,dsv,Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    // Diligent's viewport constructor takes (left,top,width,height).
    // Passing just physical width/height must never be interpreted as a
    // top-left coordinate and lose the real screen-sized Vulkan viewport.
    const auto extent=ActiveHostExtent();
    const Diligent::Viewport viewport{
        0.f,0.f,
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        0.f,1.f};
    state_->context->SetViewports(1,&viewport,extent.width,extent.height);
    const Diligent::Rect clip{0,0,int(extent.width),int(extent.height)};
    state_->context->SetScissorRects(1,&clip,extent.width,extent.height);
    return true;
  } catch (...) {AbortScene();return false;}
}
bool DiligentSceneTargets::DepthBound() const noexcept {
  return state_->bound && state_->depth_bound;
}
bool DiligentSceneTargets::SetDepthBound(bool bind) {
  if(!state_ || !state_->bound || (bind && !state_->depth))return false;
  if(state_->depth_bound==bind)return true;
  auto* rtv=state_->color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
  auto* dsv=bind ? state_->depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL) : nullptr;
  if(!rtv || (bind && !dsv))return false;
  try {
    state_->context->SetRenderTargets(1,&rtv,dsv,Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    state_->depth_bound=bind;
    return true;
  } catch (...) {AbortScene();return false;}
}
native::Size DiligentSceneTargets::ActiveHostExtent() const noexcept {
  if(!state_ || !state_->color)return {};
  const auto& desc=state_->color->GetDesc();
  return {desc.Width,desc.Height};
}
native::Size DiligentSceneTargets::ActiveSourceExtent(native::Size fallback) const noexcept {
  return state_ ? state_->formats.original_graph.SourceExtent(state_->active_color,fallback) : fallback;
}
bool DiligentSceneTargets::BindOriginalAttachments() {
  using namespace Diligent;
  if(!state_->color || (state_->depth_bound && !state_->depth))return false;
  auto* rtv=state_->color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
  auto* dsv=state_->depth_bound ? state_->depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL) : nullptr;
  if(!rtv || (state_->depth_bound && !dsv))return false;
  try {
    // XDK changes RT and depth independently. Defer Vulkan framebuffer
    // binding during the intermediate mismatched pair. Whole-frame source
    // preflight prohibits a Clear/Draw/Resolve that uses that pair.
    if(state_->depth_bound) {
      const auto& a=state_->color->GetDesc();
      const auto& b=state_->depth->GetDesc();
      if(a.Width!=b.Width || a.Height!=b.Height || a.SampleCount!=b.SampleCount) {
        state_->context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);
        return true;
      }
    }
    state_->context->SetRenderTargets(1,&rtv,dsv,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    return true;
  } catch (...) {AbortScene();return false;}
}
bool DiligentSceneTargets::SetOriginalColorTarget(uint32_t guest) {
  if(!state_ || !state_->bound || !state_->formats.original_graph.active())return false;
  const auto it=state_->colors.find(guest);
  if(it==state_->colors.end())return false;
  if(guest==state_->active_color)return true;
  state_->color=it->second;state_->active_color=guest;
  return BindOriginalAttachments();
}
bool DiligentSceneTargets::SetOriginalDepthTarget(uint32_t guest) {
  if(!state_ || !state_->bound || !state_->formats.original_graph.active())return false;
  if(guest==state_->active_depth)return true;
  const auto it=state_->depths.find(guest);
  if(guest && it==state_->depths.end())return false;
  if(guest)state_->depth=it->second;else state_->depth.Release();
  state_->active_depth=guest;state_->depth_bound=guest!=0;
  return BindOriginalAttachments();
}
bool DiligentSceneTargets::ResolveOriginalIntermediate(uint32_t guest) {
  using namespace Diligent;
  if(!state_ || !state_->bound || !state_->color ||
     !state_->formats.original_graph.active())return false;
  const auto it=state_->intermediates.find(guest);
  if(it==state_->intermediates.end() || !it->second)return false;
  const auto& src=state_->color->GetDesc();const auto& dst=it->second->GetDesc();
  if(src.Width!=dst.Width || src.Height!=dst.Height || src.Format!=dst.Format || dst.SampleCount!=1)return false;
  if(state_->depth_bound && (!state_->depth || state_->depth->GetDesc().Width!=src.Width ||
      state_->depth->GetDesc().Height!=src.Height))return false;
  auto* rtv=state_->color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
  auto* dsv=state_->depth_bound && state_->depth ?
      state_->depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL) : nullptr;
  if(!rtv || (state_->depth_bound && !dsv))return false;
  try {
    // Each original Resolve captures the active host color contents at
    // this exact chronology point. A separate texture remains readable
    // while the source RT is rebound for subsequent original draws.
    state_->context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);
    if(state_->samples>1) {
      ResolveTextureSubresourceAttribs args{};
      args.Format=Color(state_->formats.color);
      args.SrcTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
      args.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
      state_->context->ResolveTextureSubresource(state_->color,it->second,args);
    } else {
      CopyTextureAttribs args{};
      args.pSrcTexture=state_->color;args.pDstTexture=it->second;
      args.SrcTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
      args.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
      state_->context->CopyTexture(args);
    }
    state_->context->SetRenderTargets(1,&rtv,dsv,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    state_->completed_intermediates.insert(guest);
    return true;
  } catch (...) {AbortScene();return false;}
}
Diligent::ITextureView* DiligentSceneTargets::IntermediateColorSRV(uint32_t guest) const noexcept {
  if(!state_ || !state_->completed_intermediates.contains(guest))return nullptr;
  const auto it=state_->intermediates.find(guest);
  return it!=state_->intermediates.end() && it->second ?
      it->second->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}
bool DiligentSceneTargets::ClearVerifiedOriginalXdk(
    const xdk::OriginalXdkClearF& source,native::Size guest) {
  if(state_)guest=ActiveSourceExtent(guest);
  if(!state_ || !state_->prepared || !state_->bound ||
     !state_->color || ((source.flags&0x30u) && !state_->depth_bound) ||
     !xdk::IsOriginalFullTargetClear(source,guest.width,guest.height))
    return false;
  if(state_->depth_bound && (!state_->depth ||
      state_->depth->GetDesc().Width!=state_->color->GetDesc().Width ||
      state_->depth->GetDesc().Height!=state_->color->GetDesc().Height))return false;
  if((source.flags&0x0eu)!=0)
    return false; // native EoR scene owns RT0 only, never guest RT1..RT3
  if((source.flags&0x20u) &&
     state_->formats.depth!=NativeDepthFormat::D24S8)
    return false; // no stencil plane on an original D32Float depth RT
  if((source.flags&1u)!=0)
    for(const float channel:source.color)
      if(!std::isfinite(channel))return false;
  // Depth is consumed only by depth clears; stencil-only clears remain valid.
  if((source.flags&0x10u) &&
     (!std::isfinite(source.depth) || source.depth<0.f || source.depth>1.f))
    return false;
  // Validate both attachments before the first GPU clear so a missing depth
  // view cannot turn a combined original clear into a partial color clear.
  auto* color_view=(source.flags&1u) ?
      state_->color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET) : nullptr;
  auto* depth_view=(source.flags&0x30u) ?
      state_->depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL) : nullptr;
  if(((source.flags&1u) && !color_view) ||
     ((source.flags&0x30u) && !depth_view))return false;
  try {
    if(source.flags&1u) {
      state_->context->ClearRenderTarget(
          color_view,source.color.data(),
          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    if(source.flags&0x30u) {
      Diligent::CLEAR_DEPTH_STENCIL_FLAGS flags=Diligent::CLEAR_DEPTH_FLAG_NONE;
      if(source.flags&0x10u)flags|=Diligent::CLEAR_DEPTH_FLAG;
      if(source.flags&0x20u)flags|=Diligent::CLEAR_STENCIL_FLAG;
      state_->context->ClearDepthStencil(
          depth_view,flags,source.depth,source.stencil,
          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    return true;
  } catch (...) {return false;}
}
bool DiligentSceneTargets::ResolveScene() {
  if(!state_ || !state_->prepared || !state_->bound || !state_->color || !state_->resolved)return false;
  const auto& graph=state_->formats.original_graph;
  if(graph.active() && state_->active_color!=graph.final_color)return false;
  try {
    state_->context->SetRenderTargets(0,nullptr,nullptr,Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
    state_->bound=false;
    if(state_->samples>1) {
      Diligent::ResolveTextureSubresourceAttribs args;
      args.Format=Color(state_->formats.color);
      args.SrcTextureTransitionMode=Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
      args.DstTextureTransitionMode=Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
      state_->context->ResolveTextureSubresource(state_->color,state_->resolved,args);
    }
    state_->ready=true;
    state_->prepared=false; // require fresh frame validation before next BeginScene
    return true;
  } catch (...) {AbortScene();return false;}
}
Diligent::ITextureView* DiligentSceneTargets::ResolvedColorSRV() const noexcept {
  return state_ && state_->ready && state_->resolved ?
     state_->resolved->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}
} // namespace hulk::eor::gpu
