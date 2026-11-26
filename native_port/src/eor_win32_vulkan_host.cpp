#include "eor_win32_vulkan_host.h"
#include "eor_native_present_contract.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "EngineFactoryVk.h"
#include "NativeWindow.h"
#include "RefCntAutoPtr.hpp"
#include "DeviceContext.h"
#include "RenderDevice.h"
#include "SwapChain.h"
#include "Texture.h"
#include "TextureView.h"
#include "Shader.h"
#include "ShaderResourceBinding.h"
#include "ShaderResourceVariable.h"
#include "PipelineState.h"
#include <memory>
#include <utility>

namespace hulk::eor::gpu {
namespace {
// A 175% Windows scaling setting must not silently turn requested host
// pixels into virtualized logical coordinates during swapchain creation.
// Restore the worker's DPI context after querying the window so the game
// input/menu thread is unaffected.
struct PhysicalClientPixels {
  DPI_AWARENESS_CONTEXT previous=nullptr;
  PhysicalClientPixels() noexcept {
    previous=SetThreadDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  }
  ~PhysicalClientPixels() noexcept {
    if(previous)SetThreadDpiAwarenessContext(previous);
  }
};
}
struct EorWin32VulkanHost::State {
  HWND hwnd=nullptr;
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
  Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
  Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapchain;
  // Fullscreen pure-color format conversion, created only if source
  // RGBA8 and physical Win32 BGRA8 are different. No NativeX blit.
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> color_pso;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> color_srb;
};
EorWin32VulkanHost::EorWin32VulkanHost() noexcept=default;
EorWin32VulkanHost::~EorWin32VulkanHost() noexcept {Shutdown();}
bool EorWin32VulkanHost::Create(void* window) noexcept {
  if(state_ || !window)return false;
  HWND hwnd=static_cast<HWND>(window);
  PhysicalClientPixels physical;
  RECT rect{};
  if(!IsWindow(hwnd) || !GetClientRect(hwnd,&rect) ||
     rect.right<=rect.left || rect.bottom<=rect.top ||
     rect.right-rect.left>16384 || rect.bottom-rect.top>16384)
    return false;
  try {
    auto* factory=Diligent::GetEngineFactoryVk();
    if(!factory)return false;
    auto state=std::make_unique<State>();
    state->hwnd=hwnd;
    Diligent::EngineVkCreateInfo ci{};
    // EoR uses real indexed Draw/PSO state. These are native Vulkan
    // facilities; no guest PM4 processor or Xenos EDRAM surface exists.
    ci.Features.IndependentBlend=Diligent::DEVICE_FEATURE_STATE_ENABLED;
    ci.Features.DepthClamp=Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
    ci.Features.WireframeFill=Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
    ci.Features.TextureCompressionBC=Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
    ci.Features.AsyncShaderCompilation=Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
    ci.NumAsyncShaderCompilationThreads=4;
    ci.DynamicHeapSize=128u<<20;
    ci.DynamicHeapPageSize=1u<<20;
    Diligent::IDeviceContext* contexts[1]{};
    factory->CreateDeviceAndContextsVk(ci,&state->device,contexts);
    state->context.Attach(contexts[0]);
    if(!state->device || !state->context)return false;
    Diligent::SwapChainDesc desc{};
    desc.Width=static_cast<Diligent::Uint32>(rect.right-rect.left);
    desc.Height=static_cast<Diligent::Uint32>(rect.bottom-rect.top);
    // Prefer the actual BGRA8 Win32/Vulkan surface format, also observed
    // as vkFormat=44 on the owner's RTX 5070 Ti. Exact-copy presentation
    // still compares the real allocated formats and never guesses.
    desc.ColorBufferFormat=Diligent::TEX_FORMAT_BGRA8_UNORM;
    desc.DepthBufferFormat=Diligent::TEX_FORMAT_UNKNOWN;
    desc.BufferCount=3;
    desc.Usage=Diligent::SWAP_CHAIN_USAGE_RENDER_TARGET;
    Diligent::Win32NativeWindow native{hwnd};
    factory->CreateSwapChainVk(
        state->device,state->context,desc,native,&state->swapchain);
    if(!state->swapchain)return false;
    state_=std::move(state);
    return true;
  } catch (...) {
    return false;
  }
}
bool EorWin32VulkanHost::ResizeToClient() noexcept {
  if(!state_ || !IsWindow(state_->hwnd))return false;
  PhysicalClientPixels physical;
  RECT rect{};
  if(!GetClientRect(state_->hwnd,&rect) ||
     rect.right<=rect.left || rect.bottom<=rect.top ||
     rect.right-rect.left>16384 || rect.bottom-rect.top>16384)
    return false;
  try {
    const auto w=static_cast<Diligent::Uint32>(rect.right-rect.left);
    const auto h=static_cast<Diligent::Uint32>(rect.bottom-rect.top);
    const auto& old=state_->swapchain->GetDesc();
    if(w!=old.Width || h!=old.Height) {
      // Vulkan backbuffers are still referenced by work submitted for the
      // previous frame. Retire that work before replacing the swapchain.
      state_->context->SetRenderTargets(0,nullptr,nullptr,
          Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
      state_->context->Flush();
      state_->device->IdleGPU();
      state_->swapchain->Resize(w,h);
    }
    return true;
  } catch (...) {return false;}
}
bool EorWin32VulkanHost::Present(bool vsync) noexcept {
  if(!state_ || !IsWindow(state_->hwnd))return false;
  // Rendering targets the physical buffer captured at BeginScene. A resize
  // between Draw/Resolve and Present invalidates that frame: never resize
  // here and present undefined/stale pixels or start a second frame.
  PhysicalClientPixels physical;
  RECT rect{};
  if(!GetClientRect(state_->hwnd,&rect) ||
     rect.right<=rect.left || rect.bottom<=rect.top)return false;
  try {
    const auto& desc=state_->swapchain->GetDesc();
    if(desc.Width!=static_cast<Diligent::Uint32>(rect.right-rect.left) ||
       desc.Height!=static_cast<Diligent::Uint32>(rect.bottom-rect.top) ||
       !state_->swapchain->GetCurrentBackBufferRTV())return false;
    state_->swapchain->Present(vsync?1u:0u);
    return true;
  } catch (...) {return false;}
}
bool EorWin32VulkanHost::PresentOriginalColor(
    Diligent::ITextureView* source,bool vsync) noexcept {
  if(!state_ || !source || !source->GetTexture() ||
     !IsWindow(state_->hwnd))return false;
  PhysicalClientPixels physical;
  RECT rect{};
  if(!GetClientRect(state_->hwnd,&rect) ||
     rect.right<=rect.left || rect.bottom<=rect.top)return false;
  try {
    const auto& desc=state_->swapchain->GetDesc();
    if(desc.Width!=static_cast<Diligent::Uint32>(rect.right-rect.left) ||
       desc.Height!=static_cast<Diligent::Uint32>(rect.bottom-rect.top))
      return false; // the next frame must handle the resize
    auto* current=state_->swapchain->GetCurrentBackBufferRTV();
    if(!current || !current->GetTexture())return false;
    const auto& src=source->GetTexture()->GetDesc();
    const auto& dst=current->GetTexture()->GetDesc();
    const auto from=NativeColorSurface{
        src.Width,src.Height,static_cast<uint32_t>(src.Format),
        static_cast<uint32_t>(source->GetDesc().Format),
        src.SampleCount,src.ArraySize,src.MipLevels,
        source->GetDesc().ViewType==Diligent::TEXTURE_VIEW_SHADER_RESOURCE,
        source->GetDesc().ViewType==Diligent::TEXTURE_VIEW_RENDER_TARGET};
    const auto to=NativeColorSurface{
        dst.Width,dst.Height,static_cast<uint32_t>(dst.Format),
        static_cast<uint32_t>(current->GetDesc().Format),
        dst.SampleCount,dst.ArraySize,dst.MipLevels,
        false,true};
    // Both linear RGBA8 and BGRA8 denote the SAME normalized color;
    // hardware RTV store performs the component-order conversion. Exact
    // texture copies between these differently ordered formats would NOT.
    const auto linear8=[](Diligent::TEXTURE_FORMAT format) noexcept {
      return format==Diligent::TEX_FORMAT_RGBA8_UNORM ||
             format==Diligent::TEX_FORMAT_BGRA8_UNORM;
    };
    const bool valid_color_convert=linear8(src.Format) &&
        linear8(dst.Format) &&
        source->GetDesc().Format==src.Format &&
        current->GetDesc().Format==dst.Format;
    const auto path=SelectNativeColorPresentPath(
        from,to,source->GetTexture()==current->GetTexture(),
        valid_color_convert);
    if(path==NativeColorPresentPath::Reject)return false;
    // A prior native Draw may still have the backbuffer bound as RTV.
    // Detach without discarding its contents; it is already complete.
    state_->context->SetRenderTargets(0,nullptr,nullptr,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
    if(path==NativeColorPresentPath::ExactCopy) {
      Diligent::CopyTextureAttribs copy{};
      copy.pSrcTexture=source->GetTexture();
      copy.pDstTexture=current->GetTexture();
      copy.SrcTextureTransitionMode=
          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
      copy.DstTextureTransitionMode=
          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
      state_->context->CopyTexture(copy);
    } else if(path==NativeColorPresentPath::ColorConvert) {
      if(!state_->color_pso) {
        // SV_Position is the physical destination pixel coordinate.
        // Texture.Load fetches exactly ONE original EoR texel; there is
        // no bilinear, dynamic upscaling, guessed guest gamma or blur.
        constexpr const char* vs=R"(
          struct VOut { float4 pos : SV_Position; };
          VOut main(uint id : SV_VertexID) {
            float2 p=float2((id<<1)&2,id&2);
            VOut o; o.pos=float4(p*2.0-1.0,0.0,1.0); return o;
          })";
        constexpr const char* ps=R"(
          Texture2D<float4> EorColor;
          float4 main(float4 physical : SV_Position) : SV_Target {
            return EorColor.Load(int3(int2(physical.xy),0));
          })";
        Diligent::ShaderCreateInfo sh{};
        sh.SourceLanguage=Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
        sh.EntryPoint="main";
        sh.Source=vs;
        sh.Desc.ShaderType=Diligent::SHADER_TYPE_VERTEX;
        sh.Desc.Name="Original EoR native present vertex";
        Diligent::RefCntAutoPtr<Diligent::IShader> vert;
        state_->device->CreateShader(sh,&vert);
        if(!vert)return false;
        sh.Source=ps;
        sh.Desc.ShaderType=Diligent::SHADER_TYPE_PIXEL;
        sh.Desc.Name="Original EoR native present pixel";
        Diligent::RefCntAutoPtr<Diligent::IShader> frag;
        state_->device->CreateShader(sh,&frag);
        if(!frag)return false;
        Diligent::GraphicsPipelineStateCreateInfo ci{};
        ci.PSODesc.Name="Original EoR physical pixel present";
        ci.PSODesc.PipelineType=Diligent::PIPELINE_TYPE_GRAPHICS;
        ci.PSODesc.ResourceLayout.DefaultVariableType=
            Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        ci.GraphicsPipeline.NumRenderTargets=1;
        ci.GraphicsPipeline.RTVFormats[0]=dst.Format;
        ci.GraphicsPipeline.DSVFormat=Diligent::TEX_FORMAT_UNKNOWN;
        ci.GraphicsPipeline.PrimitiveTopology=
            Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ci.GraphicsPipeline.RasterizerDesc.CullMode=Diligent::CULL_MODE_NONE;
        ci.GraphicsPipeline.DepthStencilDesc.DepthEnable=false;
        ci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable=false;
        ci.pVS=vert;
        ci.pPS=frag;
        state_->device->CreateGraphicsPipelineState(ci,&state_->color_pso);
        if(!state_->color_pso)return false;
        state_->color_pso->CreateShaderResourceBinding(
            &state_->color_srb,true);
        if(!state_->color_srb)return false;
      }
      auto* variable=state_->color_srb->GetVariableByName(
          Diligent::SHADER_TYPE_PIXEL,"EorColor");
      if(!variable)return false;
      variable->Set(source);
      auto* render_target=current;
      state_->context->SetRenderTargets(1,&render_target,nullptr,
          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      Diligent::Viewport viewport{};
      viewport.Width=static_cast<float>(dst.Width);
      viewport.Height=static_cast<float>(dst.Height);
      viewport.MinDepth=0.f;viewport.MaxDepth=1.f;
      state_->context->SetViewports(1,&viewport,dst.Width,dst.Height);
      state_->context->SetPipelineState(state_->color_pso);
      state_->context->CommitShaderResources(state_->color_srb,
          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      Diligent::DrawAttribs draw{};
      draw.NumVertices=3;
      state_->context->Draw(draw);
      state_->context->SetRenderTargets(0,nullptr,nullptr,
          Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
    }
    // This method is the exclusive independent physical presenter;
    // it is never invoked from NativeX's existing FinishPresent worker.
    state_->swapchain->Present(vsync?1u:0u);
    return true;
  } catch (...) {
    // A failed GPU copy is NOT permission to present an older guest frame.
    return false;
  }
}
void EorWin32VulkanHost::Shutdown() noexcept {
  if(!state_)return;
  // The native EoR backend can have indexed Draws, resolved images and
  // dynamically mapped constants still pending on the GPU. Wait before
  // destroying the owning VkDevice/VkSwapchain; do not reuse guest fences.
  try {
    if(state_->context)state_->context->Flush();
    if(state_->device)state_->device->IdleGPU();
  } catch (...) {
    // Driver loss still needs the host resources released.
  }
  state_.reset();
}
Diligent::IRenderDevice* EorWin32VulkanHost::Device() const noexcept {
  return state_?state_->device.RawPtr():nullptr;
}
Diligent::IDeviceContext* EorWin32VulkanHost::Context() const noexcept {
  return state_?state_->context.RawPtr():nullptr;
}
Diligent::ISwapChain* EorWin32VulkanHost::SwapChain() const noexcept {
  return state_?state_->swapchain.RawPtr():nullptr;
}
NativeWin32EorCompositeOutput BindNativeWin32EorVulkanOutput(
    EorWin32VulkanHost& host,bool vsync) {
  return [&host,vsync](Diligent::ITextureView* color,
                       native::Rect rect,native::Size physical) {
    if(!color || !color->GetTexture() || !host.SwapChain())return false;
    const auto& src=color->GetTexture()->GetDesc();
    // The backend must finish exactly the original 3D scene's *physical*
    // frame; reject black-bar 16:9 output, upscales and stale dimensions.
    if(!native::HasFullNativeCompositePixels(
           {src.Width,src.Height},rect,physical))return false;
    const auto& dst=host.SwapChain()->GetDesc();
    if(dst.Width!=physical.width || dst.Height!=physical.height)
      return false;
    return host.PresentOriginalColor(color,vsync);
  };
}
} // namespace hulk::eor::gpu
#endif
