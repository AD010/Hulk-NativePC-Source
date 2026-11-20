// Read-only connection to REAL Hulk USA XDK Draw and Present wrappers.
// Never writes guest memory, never bypasses the original NativeX or Xenos.
// This is an input provenance oracle, not a completed Vulkan renderer.
#include "eor_xdk_hook_bridge.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <limits>
#include <mutex>
#include <memory>
#include <new>
#include <utility>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(hulk_eor_xdk_capture, false, "Hulk/Diagnostics",
                    "Opt-in original XDK Draw/Present call snapshot; no rendering redirection");
REXCVAR_DEFINE_BOOL(hulk_eor_vulkan_replay, false, "Hulk/Diagnostics",
                    "Opt-in direct EoR Vulkan replay; requires verified sole presenter");
namespace hulk::eor::xdk {
namespace {
std::mutex g_mutex;
FrameCapture g_collector;
XdkCallsiteEvidence g_callsite_evidence;
CompletedFrameMailbox g_frames_for_native;
// ReXGlue's guest base is NOT directly an original Xbox VA; use the same
// guest high-memory padding convention as native_camera_direct.cpp, and
// reject inaccessible ranges without reading unverified game memory.
bool ReadGuest(void* state,uint32_t address,std::byte* output,size_t length) noexcept {
#ifdef _WIN32
  auto* guest_base=static_cast<uint8_t*>(state);
  if(!guest_base || !output || !length)return false;
  const uint64_t offset=uint64_t(address)+(address>=0xE0000000u?4096u:0u);
  if(offset>UINTPTR_MAX-uintptr_t(guest_base))return false;
  const uintptr_t start=uintptr_t(guest_base)+uintptr_t(offset);
  if(length>UINTPTR_MAX-start)return false;
  // A shader/declaration/texture state block may cross two committed
  // regions with different protections. Walk every region independently,
  // copying only after verifying that part; never blindly span a gap.
  uintptr_t cursor=start;
  size_t remaining=length;
  auto* destination=output;
  while(remaining) {
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(reinterpret_cast<const void*>(cursor),&info,
                    sizeof(info))!=sizeof(info) ||
       info.State!=MEM_COMMIT || (info.Protect&PAGE_GUARD))
      return false;
    const DWORD protection=info.Protect & 0xffu;
    if(protection!=PAGE_READONLY && protection!=PAGE_READWRITE &&
       protection!=PAGE_WRITECOPY && protection!=PAGE_EXECUTE_READ &&
       protection!=PAGE_EXECUTE_READWRITE &&
       protection!=PAGE_EXECUTE_WRITECOPY)
      return false;
    const uintptr_t region=uintptr_t(info.BaseAddress);
    if(info.RegionSize>UINTPTR_MAX-region)return false;
    const uintptr_t end=region+info.RegionSize;
    if(cursor<region || cursor>=end)return false;
    const auto chunk=std::min<size_t>(remaining,end-cursor);
    std::memcpy(destination,reinterpret_cast<const void*>(cursor),chunk);
    destination+=chunk;
    remaining-=chunk;
    cursor+=chunk;
  }
  return true;
#else
  (void)state;(void)address;(void)output;(void)length;
  return false;
#endif
}
}
bool CaptureEnabled() { return REXCVAR_GET(hulk_eor_xdk_capture); }
std::optional<DeliveredFrame> TakeCompletedPpcFrame() {
  std::lock_guard lock(g_mutex);
  return g_frames_for_native.Take();
}
bool ReplayNextVerifiedPpcFrame(
    std::span<const native::VerifiedXdkResourceWitness> witnesses,
    const native::FrameGeometry& output,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& shaders,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& gpu) {
  // Refuse incomplete engine/host provenance BEFORE consuming the only
  // immutable guest Present snapshot. No guest memory, GPU render targets,
  // or Vulkan swapchain may be touched by a rejected candidate.
  if (!CaptureEnabled() || !REXCVAR_GET(hulk_eor_vulkan_replay) ||
      !output.direct_engine_path_ready ||
      !native::HasFullNativeCompositePixels(
          output.world,output.composite,output.backbuffer) ||
      witnesses.empty() || residents.empty() || !shaders.size() ||
      !effects.EffectCount() ||
      effects.ShaderCount()!=shaders.expected() ||
      reflection.size()!=shaders.expected())
    return false;
  // The caller MUST already be the exclusive render-thread presenter.
  // Inspect the immutable guest Present snapshot without holding g_mutex
  // across timeline checks. An ineligible frame remains available for replay.
  std::shared_ptr<const Frame> preview;
  uint64_t missed=0;
  {
    std::lock_guard lock(g_mutex);
    preview=g_frames_for_native.Peek();
    missed=g_frames_for_native.MissedSinceTake();
  }
  if(!preview || missed || !ValidateOriginalCommandTimeline(*preview))
    return false;
  const uint64_t sequence=preview->sequence;
  try {
    // This eligibility check is read-only: no PSO, RT or GPU writes.
    if(!gpu.CanReplayOriginalXdkTimeline(*preview))return false;
  } catch (...) {return false;}
  if(!CaptureEnabled() || !REXCVAR_GET(hulk_eor_vulkan_replay))
    return false;
  // A concurrent Present can replace the candidate while the GPU backend
  // checks the timeline. Reject stale sequence and any overwritten frames.
  preview.reset();
  std::optional<DeliveredFrame> delivered;
  {
    std::lock_guard lock(g_mutex);
    delivered=g_frames_for_native.TakeIfSequence(sequence);
  }
  if(!delivered || delivered->overwritten_since_take)return false;
  try {
    native::ReplayDeliveredResourceScene(*delivered,witnesses,output,
                                          residents,shaders,effects,reflection,gpu);
    return true;
  } catch (...) {
    // The caller retains sole-presenter ownership; failure is never a
    // license for a second Xenos/NativeX Present on this swapchain.
    if(delivered->frame.sequence<=5 || delivered->frame.sequence%600==0)
      REXLOG_WARN("[HULK/EOR-REPLAY] frame={} rejected native replay; "
                  "no verified Vulkan image claimed",delivered->frame.sequence);
    return false;
  }
}
void CapturePpcDraw(Kind kind, uint32_t guest, const PPCContext& ctx,
                    uint8_t* guest_base) {
  if (!CaptureEnabled()) return;
  RawDraw draw{
      kind, guest, static_cast<uint32_t>(ctx.lr),
      {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,
       ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32}};
  // The original source-proven USA 6534 device holds actual VS constants
  // at +1920; capture the 3D World/ViewProj/Previous/Sky registers before
  // the original Draw executes. Do not infer original EoR model identity,
  // camera owner, or SPIR-V UBO layout from these register values.
  if(kind==Kind::DrawIndexedVertices || kind==Kind::DrawVertices) {
    draw.observed_vs_matrices=CaptureUsaVsMatrices(
        ctx.r3.u32,ReadGuest,guest_base);
    if(draw.observed_vs_matrices) {
      // Only while explicit diagnostic capture is active. Store all
      // 256 original vertex AND pixel float4 registers, not just camera:
      // later source-proven native SPIR-V material declarations can read
      // exact per-Draw shader parameters without another guest memory read.
      try {
        auto constants=std::make_shared<OriginalShaderRegisters>();
        if(CaptureUsaAllShaderRegisters(ctx.r3.u32,ReadGuest,
                                        guest_base,*constants))
          draw.observed_shader_constants=std::move(constants);
      } catch (const std::bad_alloc&) {
        // Never crash game logic on a diagnostic snapshot allocation.
      }
    }
  }
  std::lock_guard lock(g_mutex);
  g_collector.Record(draw);
}
void CapturePpcCommand(CommandKind kind,uint32_t guest,
                       const PPCContext& ctx,uint8_t* guest_base) {
  if(!CaptureEnabled())return;
  RawCommand event{kind,guest,static_cast<uint32_t>(ctx.lr),
                   {ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,
                    ctx.r7.u32,ctx.r8.u32,ctx.r9.u32,ctx.r10.u32},
                   UINT32_MAX};
  // Copy original guest data while ClearF's pointers still refer to
  // the actual command arguments. No guest memory writes or RT guessing.
  if(kind==CommandKind::ClearF)
    event.captured_clear=CaptureOriginalXdkClearF(
        event.gpr,ctx.f1.f64,ReadGuest,guest_base);
  std::lock_guard lock(g_mutex);
  g_collector.RecordCommand(event);
}
void EndPpcHook(native_x_renderer::Call kind) {
  if (kind != native_x_renderer::Call::kPresent || !CaptureEnabled()) return;
  Frame frame;
  std::vector<CallsiteEvidenceRecord> hot;
  uint64_t untracked=0;
  {
    std::lock_guard lock(g_mutex);
    frame = g_collector.Finish();
    g_callsite_evidence.Observe(frame);
    if(frame.sequence<=5 || frame.sequence%600==0) {
      hot=g_callsite_evidence.HotCallsites(8);
      untracked=g_callsite_evidence.UntrackedCalls();
    }
  }
  if (frame.sequence <= 5 || frame.sequence % 600 == 0) {
    uint32_t indexed = 0, verts = 0, up = 0;
    for (const RawDraw& call : frame.calls) {
      switch (call.kind) {
      case Kind::DrawVertices: ++verts; break;
      case Kind::DrawIndexedVertices: ++indexed; break;
      case Kind::DrawVerticesUP: ++up; break;
      }
    }
    REXLOG_INFO("[HULK/EOR-XDK-PROVENANCE] frame={} real PPC: "
                "indexed={} vertices={} UP={} dropped={} "
                "commands={} dropped_commands={} complete_timeline={}; "
                "native EoR per-pass GPU ownership still source-gated",
                frame.sequence,indexed,verts,up,frame.dropped,
                frame.commands.size(),frame.dropped_commands,
                ValidateOriginalCommandTimeline(frame));
    for (const auto& site: hot) {
      REXLOG_INFO("[HULK/EOR-XDK-CALLSITE] XDK=0x{:08X} LR=0x{:08X} "
                  "kind={} draws={} frames={} first={} last={} "
                  "varying_r3_r10_mask=0x{:02X} nonzero_mask=0x{:02X} "
                  "untracked={}; guest ABI and EoR engine owner UNVERIFIED",
                  site.xdk_function,site.caller_lr,static_cast<unsigned>(site.kind),
                  site.recorded_draws,site.frames_seen,site.first_frame,
                  site.last_frame,site.varying_gpr_mask,site.nonzero_gpr_mask,
                  untracked);
    }
    // Representative REAL PPC arguments, not invented engine structure
    // offsets. One sample per reported frame is enough for an ABI oracle.
    // Never log original game memory, shader contents or raw GPU buffers.
    const auto sample = std::find_if(frame.calls.begin(), frame.calls.end(),
        [](const RawDraw& call) {
          return call.kind == Kind::DrawIndexedVertices;
        });
    if (sample != frame.calls.end()) {
      const auto& r = sample->gpr;
      REXLOG_INFO("[HULK/EOR-XDK-ABI] indexed_fn=0x{:08X} LR=0x{:08X}"
                  " PPC_r3_r10={:08X},{:08X},{:08X},{:08X},"
                  "{:08X},{:08X},{:08X},{:08X} (uninterpreted)",
                  sample->function, sample->lr, r[0], r[1], r[2], r[3],
                  r[4], r[5], r[6], r[7]);
    }
  }
  // The actual guest Present boundary is the only source of completed
  // original frames. The native Vulkan consumer receives this real,
  // immutable PPC Draw/matrix/object snapshot through TakeCompletedPpcFrame.
  // Keep Xenos as presenter until verified materials and full shader
  // coverage authorize an independent handoff.
  {
    std::lock_guard lock(g_mutex);
    g_frames_for_native.Publish(std::move(frame));
  }
}
}  // namespace hulk::eor::xdk
