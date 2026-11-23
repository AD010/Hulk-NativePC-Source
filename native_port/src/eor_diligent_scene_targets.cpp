#include "eor_diligent_scene_targets.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Texture.h"
#include "TextureView.h"
#include "RefCntAutoPtr.hpp"
#include <cmath>
#include <limits>
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
  native::Size physical{};
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
}
void DiligentSceneTargets::Reset() noexcept {
  if(!state_)return;
  AbortScene();
  state_->resolved.Release();state_->depth.Release();state_->color.Release();
  state_->physical={};state_->formats={};state_->samples=0;
}
bool DiligentSceneTargets::Prepare(const native::FrameGeometry& frame,
                                  VerifiedSceneFormat verified) {
  // A nested Prepare must not invalidate an active render pass. In
  // particular ResolveScene still needs the original prepared state.
  if (state_->bound) return false;
  // Never allow BeginScene after Prepare fails or returns an old allocation.
  state_->prepared=false;
  state_->ready=false;
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
  if(Same(state_->physical,frame.world) && state_->samples==frame.samples &&
     state_->formats.color==verified.color && state_->formats.depth==verified.depth &&
     state_->color && (state_->depth || verified.depth==NativeDepthFormat::None) && state_->resolved) {
    // Native attachment storage is reusable at the same extent/format,
    // but the verified Xbox RT0/depth/front-buffer *owner* can change
    // after a menu transition, loading screen or resource recreation.
    // Never retain last Present's source identity with the new frame.
    state_->formats=verified;
    state_->prepared=true;
    return true;
  }
  // Transactional attachment allocation: a failure keeps the preceding
  // complete target set, rather than presenting an empty/half-size image.
  Diligent::RefCntAutoPtr<Diligent::ITexture> new_color,new_depth,new_resolved;
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
  try {
    state_->device->CreateTexture(desc,nullptr,&new_color);
    if(!new_color || !new_color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET))return false;
    if(verified.depth!=NativeDepthFormat::None) {
      desc.Name="EoR verified host depth";
      desc.Format=Depth(verified.depth);
      desc.BindFlags=Diligent::BIND_DEPTH_STENCIL;
      state_->device->CreateTexture(desc,nullptr,&new_depth);
      if(!new_depth || !new_depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL))return false;
    }
    if(frame.samples!=1) {
      desc.Name="EoR resolved host scene color";
      desc.SampleCount=1;desc.Format=Color(verified.color);
      desc.BindFlags=Diligent::BIND_FLAGS(Diligent::BIND_RENDER_TARGET|Diligent::BIND_SHADER_RESOURCE);
      state_->device->CreateTexture(desc,nullptr,&new_resolved);
      if(!new_resolved || !new_resolved->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE))return false;
    } else {
      new_resolved=new_color;
      if(!new_resolved->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE))return false;
    }
  } catch (...) {return false;}
  state_->color=std::move(new_color);
  state_->depth=std::move(new_depth);
  state_->resolved=std::move(new_resolved);
  state_->physical=frame.world;
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
    state_->depth_bound=state_->formats.depth!=NativeDepthFormat::None;
    auto* dsv=state_->depth ? state_->depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL) : nullptr;
    if(!rtv || (state_->depth_bound && !dsv))return false;
    // A driver may bind the target and then throw (device loss/resize).
    // Record ownership first so AbortScene always attempts to detach it.
    state_->bound=true;
    state_->context->SetRenderTargets(1,&rtv,dsv,Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    // Diligent's viewport constructor takes (left,top,width,height).
    // Passing just physical width/height must never be interpreted as a
    // top-left coordinate and lose the real screen-sized Vulkan viewport.
    const Diligent::Viewport viewport{
        0.f,0.f,
        static_cast<float>(state_->physical.width),
        static_cast<float>(state_->physical.height),
        0.f,1.f};
    state_->context->SetViewports(1,&viewport,state_->physical.width,state_->physical.height);
    const Diligent::Rect clip{0,0,int(state_->physical.width),int(state_->physical.height)};
    state_->context->SetScissorRects(1,&clip,state_->physical.width,state_->physical.height);
    return true;
  } catch (...) {AbortScene();return false;}
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
bool DiligentSceneTargets::ClearVerifiedOriginalXdk(
    const xdk::OriginalXdkClearF& source,native::Size guest) {
  if(!state_ || !state_->prepared || !state_->bound ||
     !state_->color || ((source.flags&0x30u) && !state_->depth_bound) ||
     !xdk::IsOriginalFullTargetClear(source,guest.width,guest.height))
    return false;
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
