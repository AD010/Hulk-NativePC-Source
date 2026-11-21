// Read-only connection to REAL Hulk USA XDK Draw and Present wrappers.
// Never writes guest memory, never bypasses the original NativeX or Xenos.
// This is an input provenance oracle, not a completed Vulkan renderer.
#include "eor_xdk_hook_bridge.h"
#include "device.h"
#include "SwapChain.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <limits>
#include <mutex>
#include <memory>
#include <new>
#include <utility>
#include <variant>
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
std::atomic<uint64_t> g_eor_ready_backbuffers{0};
std::atomic<uint64_t> g_eor_gpu_failures{0};
FrameCapture g_collector;
XdkCallsiteEvidence g_callsite_evidence;
CompletedFrameMailbox g_frames_for_native;
std::shared_ptr<const VerifiedWorkerEorFrame> g_verified_worker_frame;
struct OfflineShaderPack {
  std::shared_ptr<const aot::ShaderBank> shaders;
  std::shared_ptr<const aot::EffectCatalog> effects;
  std::shared_ptr<const aot::ReflectionBank> reflection;
};
std::optional<OfflineShaderPack> g_offline_shader_pack;
OriginalGuestBufferSnapshots g_original_buffers; // guarded by g_mutex
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

// Original USA XDK 6534 resource object: GPU buffer address at +24,
// allocation byte length at +28 (both PPC big endian). This capture NEVER
// trusts the address without the checked ReadGuest, nor logs game bytes.
std::shared_ptr<const OriginalGuestBufferBytes> SourceGpuBuffer(
    uint32_t object,void* guest_base) {
  if(!object || object>UINT32_MAX-32u)return {};
  std::array<std::byte,8> meta{};
  if(!ReadGuest(guest_base,object+24u,meta.data(),meta.size()))return {};
  const auto be32=[](const std::byte* p) {
    return uint32_t(std::to_integer<uint8_t>(p[0]))<<24 |
           uint32_t(std::to_integer<uint8_t>(p[1]))<<16 |
           uint32_t(std::to_integer<uint8_t>(p[2]))<<8 |
           uint32_t(std::to_integer<uint8_t>(p[3]));
  };
  const uint32_t address=be32(meta.data()),bytes=be32(meta.data()+4);
  if(!address || bytes<6 || bytes>(8u<<20) ||
     uint64_t(address)+bytes>uint64_t(UINT32_MAX)+1u)return {};
  // Capture still reads current bytes on every Draw. Deduplicate storage,
  // not memory observations: guest dynamic buffers can change in place.
  return g_original_buffers.Capture(object,address,bytes,ReadGuest,guest_base);
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
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& gpu,
    uint64_t expected_original_frame_sequence,
    bool* reached_gpu) {
  // Preserve a truthful outcome for the caller: some failures happen
  // before ANY Vulkan work and must retain the existing NativeX image.
  if(reached_gpu)*reached_gpu=false;
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
  if(!preview)return false;
  const uint64_t sequence=preview->sequence;
  // A frame prepared from the genuine EoR engine must not be paired with
  // another XDK Present that arrived during material/PSO preparation.
  if(expected_original_frame_sequence &&
     sequence!=expected_original_frame_sequence)return false;
  // A worker stalled behind the PPC producer can lose one or more complete
  // mailbox frames. Reject the newest frame if any intervening Present was
  // overwritten, but DISCARD that stale epoch by exact sequence; otherwise
  // MissedSinceTake stays nonzero forever and native replay can never recover.
  // The next independent fully captured frame starts with zero missed loss.
  if(missed) {
    preview.reset(); // release immutable ownership before TakeIfSequence
    std::lock_guard lock(g_mutex);
    (void)g_frames_for_native.TakeIfSequence(sequence);
    return false;
  }
  if(!ValidateOriginalCommandTimeline(*preview))return false;
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
    // All previous checks are read-only. This is the FIRST point at which
    // native GPU work may start. Never classify earlier rejections as
    // device-render failures that suppress the working NativeX Present.
    // Source-only ABI/material/PSO rejection is not a GPU failure.
    // The AOT forwarding backend marks this only at actual BeginScene,
    // BEFORE a possible partial Vulkan attachment bind or driver throw.
    native::ReplayDeliveredResourceScene(*delivered,witnesses,output,
                                          residents,shaders,effects,reflection,gpu,
                                          reached_gpu);
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
  // Keep buffer revisions and their Draw in the same Present epoch.
  std::lock_guard lock(g_mutex);
  // Only a requested direct Vulkan render performs byte-exact guest
  // resource capture; diagnostics-only XDK observer is unaffected.
  if(REXCVAR_GET(hulk_eor_vulkan_replay) && draw.observed_vs_matrices) {
    try {
      const auto& state=*draw.observed_vs_matrices;
      // Source snapshots are deduplicated by the existing per-Present
      // full-byte checked cache. Repeated Xbox Draws retain identical GPU
      // buffer owners; changed vertex skinning creates a new generation.
      for(size_t slot=0;slot<draw.source_vertex_streams.size();++slot) {
        const auto object=state.original_vertex_buffer_guest_objects[slot];
        if(object)
          draw.source_vertex_streams[slot]=SourceGpuBuffer(object,guest_base);
      }
      draw.source_vb0=draw.source_vertex_streams[0];
      if(kind==Kind::DrawIndexedVertices)
        draw.source_ib=SourceGpuBuffer(
            state.original_index_buffer_guest_object,guest_base);
    } catch (const std::bad_alloc&) {
      draw.source_vb0.reset();
      draw.source_ib.reset();
      draw.source_vertex_streams={};
    }
  }
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
    g_original_buffers.Reset();
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
bool InstallVerifiedSourceShaderPack(
    std::shared_ptr<const aot::ShaderBank> shaders,
    std::shared_ptr<const aot::EffectCatalog> effects,
    std::shared_ptr<const aot::ReflectionBank> reflection) {
  if(!shaders || !shaders->size() || !shaders->expected() ||
     !effects || !effects->EffectCount() ||
     effects->ShaderCount()!=shaders->expected() ||
     !reflection || reflection->size()!=shaders->expected())
    return false;
  std::lock_guard lock(g_mutex);
  // A new original ShaderEffects generation must NEVER be attached to
  // an already queued frame from the previous bank's digest identities.
  g_verified_worker_frame.reset();
  g_offline_shader_pack=OfflineShaderPack{
      std::move(shaders),std::move(effects),std::move(reflection)};
  return true;
}
void ClearVerifiedSourceShaderPack() noexcept {
  std::lock_guard lock(g_mutex);
  g_verified_worker_frame.reset();
  g_offline_shader_pack.reset();
}
bool InstallVerifiedWorkerEorFrame(
    std::shared_ptr<const VerifiedWorkerEorFrame> frame) {
  if(!frame || !frame->original_frame_sequence || !frame->device ||
     !frame->output.direct_engine_path_ready ||
     !native::HasFullNativeCompositePixels(
        frame->output.world,frame->output.composite,frame->output.backbuffer) ||
     frame->witnesses.empty() || frame->residents.empty() ||
     !frame->gpu)
    return false;
  for(const auto& model:frame->residents)
    if(!model || !model->original_resource_hash)return false;
  for(const auto& witness:frame->witnesses) {
    const bool same_frame=std::visit([&](const auto& original) {
      return original.native_draw.original_frame_sequence ==
          frame->original_frame_sequence;
    },witness.original_source_submission);
    if(!same_frame)return false;
  }
  std::lock_guard lock(g_mutex);
  if(!g_offline_shader_pack)return false;
  const auto& pack=*g_offline_shader_pack;
  // Never permit a caller's independently constructed ShaderEffects
  // generation to be mixed with the app's actual loaded USA source pack.
  if((frame->shaders && frame->shaders!=pack.shaders) ||
     (frame->effects && frame->effects!=pack.effects) ||
     (frame->reflection && frame->reflection!=pack.reflection))
    return false;
  auto complete=std::make_shared<VerifiedWorkerEorFrame>(*frame);
  complete->shaders=pack.shaders;
  complete->effects=pack.effects;
  complete->reflection=pack.reflection;
  g_verified_worker_frame=std::move(complete);
  return true;
}
bool InstallVerifiedWorkerEorFrameWithResidents(
    std::shared_ptr<const VerifiedWorkerEorFrame> frame,
    const native::VerifiedOriginalResidentLookup& lookup) {
  if(!frame || !frame->original_frame_sequence ||
     frame->witnesses.empty() || !lookup)
    return false;
  std::shared_ptr<const Frame> source;
  {
    std::lock_guard lock(g_mutex);
    source=g_frames_for_native.Peek();
    if(!source || source->sequence!=frame->original_frame_sequence ||
       g_frames_for_native.MissedSinceTake()!=0)
      return false;
  }
  try {
    // Original EoR model/mesh/surface owner must already have been proven
    // by actual engine ABI. This stage uses the existing USA archive stream
    // GPU buffers, not reconstructed or guessed XDK shader state.
    auto residents=native::RetainVerifiedOriginalFrameResidents(
        *source,frame->witnesses,lookup);
    if(residents.empty())return false;
    auto complete=std::make_shared<VerifiedWorkerEorFrame>(*frame);
    complete->residents=std::move(residents);
    // A newer XDK Present can replace source while retrieving models.
    // Leave the final exact frame check to TryWorkerEorFrame and replay.
    return InstallVerifiedWorkerEorFrame(std::move(complete));
  } catch (...) {
    // No GPU work started. Keep the established NativeX presentation.
    return false;
  }
}
void ClearVerifiedWorkerEorFrame() noexcept {
  std::lock_guard lock(g_mutex);
  g_verified_worker_frame.reset();
}
EorWorkerStatus GetEorWorkerStatus() noexcept {
  return {g_eor_ready_backbuffers.load(std::memory_order_relaxed),
          g_eor_gpu_failures.load(std::memory_order_relaxed)};
}
native_x_renderer::HulkEorPresentResult TryWorkerEorFrame(
    native_x_renderer::Device& host) noexcept {
  using Result=native_x_renderer::HulkEorPresentResult;
  if(!CaptureEnabled() || !REXCVAR_GET(hulk_eor_vulkan_replay))
    return Result::kNotReady;
  std::shared_ptr<const VerifiedWorkerEorFrame> ready;
  std::shared_ptr<const Frame> source_snapshot;
  uint64_t source_sequence=0;
  {
    std::lock_guard lock(g_mutex);
    ready=g_verified_worker_frame;
    if(!ready)return Result::kNotReady;
    source_snapshot=g_frames_for_native.Peek();
    if(!source_snapshot)return Result::kNotReady;
    source_sequence=source_snapshot->sequence;
    if(g_frames_for_native.MissedSinceTake()) {
      // A dropped mailbox epoch is never permitted to touch Vulkan.
      // Discard it atomically while still under the original Present lock,
      // release the temporary preview first, and let the NEXT frame compete.
      source_snapshot.reset();
      (void)g_frames_for_native.TakeIfSequence(source_sequence);
      return Result::kNotReady;
    }
  }
  if(source_sequence!=ready->original_frame_sequence ||
     ready->device!=&host || !host.swap_chain())
    return Result::kNotReady;
  const auto& desc=host.swap_chain()->GetDesc();
  if(desc.Width!=ready->output.backbuffer.width ||
     desc.Height!=ready->output.backbuffer.height)
    return Result::kNotReady; // resize requires a fresh exact source frame
  std::vector<const model::UploadedOriginalModel*> residents;
  try {
    residents.reserve(ready->residents.size());
    for(const auto& model:ready->residents)residents.push_back(model.get());
    // Verify the ACTUAL original Draw -> guest resource -> native mesh/surface
    // correspondence, paired shader effects and real original SPIR-V BEFORE
    // the worker opens a Vulkan render target. This does not infer an EoR
    // model from LR, a shader hash or an adjacent XDK texture.
    if(!ValidateOriginalCommandTimeline(*source_snapshot) ||
       !ready->gpu->CanReplayOriginalXdkTimeline(*source_snapshot))
      return Result::kNotReady;
    const auto source=native::ResolveCapturedResourceDraws(
        *source_snapshot,ready->witnesses,residents);
    if(source.size()!=source_snapshot->calls.size())
      return Result::kNotReady;
    const auto bound=native::BindVerifiedGeometryDraws(
        source,residents,*ready->effects);
    if(bound.size()!=source.size())return Result::kNotReady;
    for(const auto& draw:bound) {
      if(!ready->shaders->Find(draw.vertex_source_sha256,aot::Stage::Vertex) ||
         !ready->shaders->Find(draw.pixel_source_sha256,aot::Stage::Pixel))
        return Result::kNotReady;
    }
  } catch (...) {
    // Pure source/ownership preflight has not touched Diligent.
    // Preserve the normally visible NativeX presentation on rejection.
    return Result::kNotReady;
  }
  // Release the read-only preview so the completed mailbox's exact
  // sequence Take can move the original frame without extra ownership.
  source_snapshot.reset();
  try {
    // This is the REAL sole Vulkan render worker. The final exact-sequence
    // claim is repeated atomically inside ReplayNextVerifiedPpcFrame; a
    // racing newer guest Present can never render with this old EoR owner.
    bool reached_gpu=false;
    const bool submitted=ReplayNextVerifiedPpcFrame(
        ready->witnesses,ready->output,residents,
        *ready->shaders,*ready->effects,*ready->reflection,*ready->gpu,
        source_sequence,&reached_gpu);
    // Safe preflight loss, disabled capture and a newer mailbox frame
    // MUST preserve the visible NativeX frame. Never black-screen a
    // clean rejection; only suppress fallback if native GPU work began.
    if(submitted) {
      g_eor_ready_backbuffers.fetch_add(1,std::memory_order_relaxed);
      return Result::kBackbufferReady;
    }
    if(reached_gpu) {
      g_eor_gpu_failures.fetch_add(1,std::memory_order_relaxed);
      return Result::kFailedAfterGpu;
    }
    return Result::kNotReady;
  } catch (...) {
    // An exception after potential GPU state changes cannot fall back to
    // a competing NativeX composite on the same Vulkan backbuffer.
    g_eor_gpu_failures.fetch_add(1,std::memory_order_relaxed);
    return Result::kFailedAfterGpu;
  }
}
}  // namespace hulk::eor::xdk
