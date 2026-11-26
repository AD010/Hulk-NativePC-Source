#include "eor_native_frame_executor.h"
#include "eor_diligent_aot_backend.h"
#include "DeviceContext.h"
#include "RenderDevice.h"
#include "SwapChain.h"
#include "Fence.h"
#include "RefCntAutoPtr.hpp"
#include <deque>
#include <utility>

namespace hulk::eor::gpu {
struct EorNativeFrameExecutor::State {
  struct Pending {
    uint64_t value=0,source_frame=0;
    std::vector<std::shared_ptr<const void>> owners;
  };
  EorWin32VulkanHost* host=nullptr;
  DiligentEorDirectBackend* backend=nullptr;
  const aot::ShaderBank* shaders=nullptr;
  const aot::EffectCatalog* effects=nullptr;
  const aot::ReflectionBank* reflection=nullptr;
  Diligent::RefCntAutoPtr<Diligent::IFence> fence;
  uint64_t last_signal=0;
  uint64_t last_submitted_frame=0;
  std::deque<Pending> pending;
};
EorNativeFrameExecutor::EorNativeFrameExecutor(
    EorWin32VulkanHost& host,DiligentEorDirectBackend& backend,
    const aot::ShaderBank& shaders,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection) noexcept {
  try {
    auto state=std::make_unique<State>();
    state->host=&host;state->backend=&backend;state->shaders=&shaders;
    state->effects=&effects;state->reflection=&reflection;
    if(host.Device() && host.Context()) {
      Diligent::FenceDesc desc{};
      desc.Name="Native EoR original Draw completion";
      desc.Type=Diligent::FENCE_TYPE_CPU_WAIT_ONLY;
      // Vulkan drivers may reject fence allocation during resize or under
      // GPU-memory pressure. Keep the executable native scene runner and
      // fall back to real WaitForIdle; do not silently disable all Draws.
      try {host.Device()->CreateFence(desc,&state->fence);}
      catch (...) {state->fence.Release();}
    }
    state_=std::move(state);
  } catch (...) {
    // Without a fence, the executor still has a correct synchronous path.
  }
}
EorNativeFrameExecutor::~EorNativeFrameExecutor() noexcept {Shutdown();}
void EorNativeFrameExecutor::RetireCompleted() noexcept {
  if(!state_ || !state_->fence || !state_->backend)return;
  try {
    const uint64_t done=state_->fence->GetCompletedValue();
    uint64_t last=0;
    while(!state_->pending.empty() &&
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
  if(!state_ || !state_->host || !state_->backend ||
     !state_->host->Device() || !state_->host->Context() ||
     !state_->host->SwapChain() || !state_->shaders ||
     !state_->effects || !state_->reflection || !source.sequence ||
     draws.empty() || draws.size()!=source.calls.size() ||
     source.sequence<=state_->last_submitted_frame ||
     !xdk::ValidateOriginalCommandTimeline(source) ||
     !state_->backend->CanReplayOriginalXdkTimeline(source))
    return NativeFrameSubmission::IncompleteSource;
  for(size_t i=0;i<draws.size();++i)
    if(draws[i].draw.source_frame!=source.sequence ||
       draws[i].draw.source_ordinal!=i)
      return NativeFrameSubmission::IncompleteSource;
  const auto& frame=physical_frame;
  if(!native::HasFullNativeCompositePixels(frame.world,frame.composite,
       frame.backbuffer))
    return NativeFrameSubmission::IncompleteSource;
  // Resize BEFORE DrawIndexed, never inside Present.
  if(!state_->host->ResizeToClient())return NativeFrameSubmission::IncompleteSource;
  const auto& swap=state_->host->SwapChain()->GetDesc();
  if(frame.backbuffer.width!=swap.Width ||
     frame.backbuffer.height!=swap.Height)
    return NativeFrameSubmission::IncompleteSource;

  RetireCompleted();
  bool began_gpu=false;
  try {
    aot::ReplayAotScene(frame,draws,*state_->shaders,*state_->effects,
        *state_->reflection,*state_->backend,&source,&began_gpu);
    auto* context=state_->host->Context();
    if(state_->fence && state_->last_signal!=UINT64_MAX) {
      const uint64_t signal=state_->last_signal+1;
      // Reserve the retained ownership BEFORE GPU submission: if memory
      // allocation fails, the exception handler waits for GPU completion.
      state_->pending.push_back(State::Pending{signal,source.sequence,
                                               std::move(retained)});
      context->EnqueueSignal(state_->fence,signal);
      context->Flush();
      state_->last_signal=signal;
      RetireCompleted();
      if(state_->pending.size()>3) {
        // Only actual sustained GPU backlog blocks this native worker.
        context->WaitForIdle();
        RetireCompleted();
      }
    } else {
      // No Vulkan fence available: retain all owners through completion.
      context->WaitForIdle();
      state_->backend->RetireCompletedOriginalFontDraws(source.sequence);
    }
    state_->last_submitted_frame=source.sequence;
    return NativeFrameSubmission::Submitted;
  } catch (...) {
    if(began_gpu) {
      // The GPU may have consumed original model/font buffers already.
      // Never allow fallback/cleanup while an unordered Draw is in flight.
      try {
        auto* context=state_->host->Context();
        context->Flush();context->WaitForIdle();
        // A signal may have been enqueued but failed to publish a fence
        // value. WaitForIdle is now real GPU completion for ALL pending
        // frames; empty retained host assets only after this barrier.
        state_->pending.clear();
        state_->backend->RetireCompletedOriginalFontDraws(source.sequence);
        state_->last_submitted_frame=source.sequence;
      } catch (...) {}
      return NativeFrameSubmission::FailedAfterGpu;
    }
    state_->backend->DiscardUnsubmittedOriginalFontDraws(source.sequence);
    return NativeFrameSubmission::IncompleteSource;
  }
}
void EorNativeFrameExecutor::Shutdown() noexcept {
  if(!state_)return;
  if(!state_->pending.empty()) {
    try {
      auto* context=state_->host?state_->host->Context():nullptr;
      if(context) {
        context->Flush();context->WaitForIdle();
        RetireCompleted();
      }
    } catch (...) {}
  }
  // Release pending original asset owners only after GPU completion.
  // On device loss, the renderer's device teardown owns final cleanup.
  state_.reset();
}
} // namespace hulk::eor::gpu
