#include "eor_native_frame_executor.h"
#include "eor_diligent_aot_backend.h"
#include "DeviceContext.h"
#include "RenderDevice.h"
#include "SwapChain.h"
#include "Fence.h"
#include "RefCntAutoPtr.hpp"
#include <deque>
#include <array>
#include <algorithm>
#include <cstring>
#include <exception>
#include <utility>

namespace hulk::eor::gpu {
struct EorNativeFrameExecutor::State {
  struct Pending {
    uint64_t value=0,source_frame=0;
    std::vector<std::shared_ptr<const void>> owners;
  };
  EorWin32VulkanHost* host=nullptr;
  Diligent::IRenderDevice* device=nullptr;
  Diligent::IDeviceContext* context=nullptr;
  Diligent::ISwapChain* swapchain=nullptr;
  DiligentEorDirectBackend* backend=nullptr;
  const aot::ShaderBank* shaders=nullptr;
  const aot::EffectCatalog* effects=nullptr;
  const aot::ReflectionBank* reflection=nullptr;
  Diligent::RefCntAutoPtr<Diligent::IFence> fence;
  uint64_t last_signal=0;
  uint64_t last_submitted_frame=0;
  bool completion_failed=false;
  std::array<char,768> failure{};
  void SetFailure(const char* message) noexcept {
    const auto n=(std::min)(std::strlen(message),failure.size()-1);
    std::memcpy(failure.data(),message,n);failure[n]=0;
  }
  std::deque<Pending> pending;
};
EorNativeFrameExecutor::EorNativeFrameExecutor(
    EorWin32VulkanHost& host,DiligentEorDirectBackend& backend,
    const aot::ShaderBank& shaders,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection) noexcept
    :EorNativeFrameExecutor(host.Device(),host.Context(),host.SwapChain(),
                            backend,shaders,effects,reflection) {
  if(state_)state_->host=&host;
}
EorNativeFrameExecutor::EorNativeFrameExecutor(
    Diligent::IRenderDevice* device,Diligent::IDeviceContext* context,
    Diligent::ISwapChain* swapchain,DiligentEorDirectBackend& backend,
    const aot::ShaderBank& shaders,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection) noexcept {
  try {
    auto state=std::make_unique<State>();
    state->device=device;state->context=context;state->swapchain=swapchain;
    state->backend=&backend;state->shaders=&shaders;
    state->effects=&effects;state->reflection=&reflection;
    if(device && context && swapchain) {
      Diligent::FenceDesc desc{};
      desc.Name="Native EoR original Draw completion";
      desc.Type=Diligent::FENCE_TYPE_CPU_WAIT_ONLY;
      // Vulkan drivers may reject fence allocation during resize or under
      // GPU-memory pressure. Keep the executable native scene runner and
      // fall back to real WaitForIdle; do not silently disable all Draws.
      try {device->CreateFence(desc,&state->fence);}
      catch (...) {state->fence.Release();}
    }
    state_=std::move(state);
  } catch (...) {
    // Without a fence, the executor still has a correct synchronous path.
  }
}
EorNativeFrameExecutor::~EorNativeFrameExecutor() noexcept {Shutdown();}
const char* EorNativeFrameExecutor::LastFailure() const noexcept {
  return state_?state_->failure.data():"Native frame executor not allocated";
}
void EorNativeFrameExecutor::RetireCompleted() noexcept {
  if(!state_ || !state_->fence || !state_->backend)return;
  try {
    const uint64_t done=state_->fence->GetCompletedValue();
    uint64_t last=0;
    while(!state_->pending.empty() && state_->pending.front().value &&
          state_->pending.front().value<=done) {
      last=state_->pending.front().source_frame;
      state_->pending.pop_front();
    }
    if(last)state_->backend->RetireCompletedOriginalFontDraws(last);
  } catch (...) {
    // A lost Vulkan device cannot authorize early freeing of GPU buffers.
  }
}
NativeFrameSubmission EorNativeFrameExecutor::Submit(
    const native::FrameGeometry& physical_frame,
    std::span<const aot::BoundIndexedDraw> draws,
    const xdk::Frame& source,
    std::vector<std::shared_ptr<const void>> retained) noexcept {
  if(state_)state_->SetFailure("");
  if(state_ && state_->completion_failed) {
    state_->SetFailure("Previous GPU completion failed; device recovery required");
    return NativeFrameSubmission::FailedAfterGpu;
  }
  const auto reject=[this](const char* reason) noexcept {
    if(state_)state_->SetFailure(reason);
    return NativeFrameSubmission::IncompleteSource;
  };
  const auto& frame=physical_frame;
  // Timeline planning can allocate and the driver may throw on descriptor
  // queries. A rejected pre-Draw frame must not escape this noexcept API
  // or discard GPU owners retained by an earlier submission.
  try {
    if(!state_ || !state_->backend || !state_->device ||
       !state_->context || !state_->swapchain ||
       (state_->host && (state_->host->Device()!=state_->device ||
                         state_->host->Context()!=state_->context ||
                         state_->host->SwapChain()!=state_->swapchain)) ||
       !state_->backend->UsesNativeHostGpu(state_->device,state_->context) ||
       !state_->shaders || !state_->effects || !state_->reflection || !source.sequence ||
       draws.empty() || draws.size()!=source.calls.size() ||
       source.sequence<=state_->last_submitted_frame ||
       !xdk::ValidateOriginalCommandTimeline(source) ||
       !state_->backend->CanReplayOriginalXdkTimeline(source))
      return reject("Original frame ownership, timeline or shader bank validation failed");
    for(size_t i=0;i<draws.size();++i)
      if(draws[i].draw.source_frame!=source.sequence ||
         draws[i].draw.source_ordinal!=i)
        return reject("Original Draw sequence does not match native order");
    if(!native::HasFullNativeCompositePixels(frame.world,frame.composite,
         frame.backbuffer))
      return reject("Native physical frame extent is incomplete");
    // Resize BEFORE DrawIndexed, never inside Present.
    // Borrowed worker handles are already resized by their sole owner.
    // Never resize or physically Present a swapchain owned by that worker.
    if(state_->host && !state_->host->ResizeToClient())return reject("Native swapchain resize unavailable");
    const auto& swap=state_->swapchain->GetDesc();
    if(frame.backbuffer.width!=swap.Width ||
       frame.backbuffer.height!=swap.Height)
      return reject("Native swapchain extent changed");
  } catch (const std::exception& e) {
    return reject(e.what());
  } catch (...) {
    return reject("Unknown native frame validation exception");
  }

  RetireCompleted();
  // Allocate the ownership record BEFORE ReplayAotScene can submit any GPU
  // work. Moving owners into a temporary deque element after Draw can free
  // them on allocation failure before the exception handler waits for idle.
  try {state_->pending.emplace_back();}
  catch (...) {return reject("Native GPU ownership allocation failed");}
  auto& current=state_->pending.back();
  current.source_frame=source.sequence;
  current.owners.swap(retained); // nonthrowing; value=0 means not fenced yet
  bool began_gpu=false;
  try {
    aot::ReplayAotScene(frame,draws,*state_->shaders,*state_->effects,
        *state_->reflection,*state_->backend,&source,&began_gpu);
    auto* context=state_->context;
    if(state_->fence && state_->last_signal!=UINT64_MAX) {
      const uint64_t signal=state_->last_signal+1;
      context->EnqueueSignal(state_->fence,signal);
      context->Flush();
      current.value=signal;
      state_->last_signal=signal;
      RetireCompleted();
      if(state_->pending.size()>3) {
        // One completed epoch frees a slot. Leave later submissions in flight.
        state_->fence->Wait(state_->pending.front().value);
        RetireCompleted();
      }
    } else {
      // No Vulkan fence available: retain all owners through completion.
      context->Flush();
      context->WaitForIdle();
      state_->pending.clear();
      state_->backend->RetireCompletedOriginalFontDraws(source.sequence);
    }
    state_->last_submitted_frame=source.sequence;
    return NativeFrameSubmission::Submitted;
  } catch (...) {
    // Preserve the original Draw/binding failure before cleanup changes
    // the state. Fixed-size storage cannot throw from this noexcept API.
    try {throw;}
    catch(const std::exception& e){state_->SetFailure(e.what());}
    catch(...){state_->SetFailure("Unknown native GPU replay exception");}
    if(began_gpu) {
      // A partially submitted frame must never be replayed or replaced by
      // a competing renderer, even if the completion barrier itself fails.
      state_->last_submitted_frame=source.sequence;
      try {
        auto* context=state_->context;
        context->Flush();context->WaitForIdle();
        state_->pending.clear();
        state_->backend->RetireCompletedOriginalFontDraws(source.sequence);
      } catch (...) {
        // Retain unfenced owners, forbid further submissions on this
        // executor, and require host/device recovery before reuse.
        state_->completion_failed=true;
      }
      return NativeFrameSubmission::FailedAfterGpu;
    }
    state_->pending.pop_back(); // no GPU work consumed this record
    state_->backend->DiscardUnsubmittedOriginalFontDraws(source.sequence);
    return NativeFrameSubmission::IncompleteSource;
  }
}
void EorNativeFrameExecutor::Shutdown() noexcept {
  if(!state_)return;
  if(!state_->pending.empty()) {
    try {
      auto* context=state_->context;
      if(context) {
        context->Flush();context->WaitForIdle();
        const auto last=state_->pending.back().source_frame;
        state_->pending.clear();
        state_->backend->RetireCompletedOriginalFontDraws(last);
      }
    } catch (...) {}
  }
  // Release pending original asset owners only after GPU completion.
  // On device loss, the renderer's device teardown owns final cleanup.
  state_.reset();
}
} // namespace hulk::eor::gpu
