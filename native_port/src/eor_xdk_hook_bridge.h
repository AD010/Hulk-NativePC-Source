#pragma once
#include "eor_xdk_draw_capture.h"
#include "eor_native_draw_geometry.h"
#include <rex/ppc/context.h>
#include <native_x_renderer/native_x_renderer.h>

namespace hulk::eor::xdk {
bool CaptureEnabled();
void CapturePpcDraw(Kind kind, uint32_t guest, const PPCContext& ctx,
                    uint8_t* guest_base);
void CapturePpcCommand(CommandKind kind, uint32_t guest,
                       const PPCContext& ctx,uint8_t* guest_base);
void EndPpcHook(native_x_renderer::Call kind);
// The only completed read-only guest frame handoff to a source-verified
// native renderer. Callers must reject missing witnesses, missed frames,
// wrong resource ownership and incomplete shader bank BEFORE Vulkan BeginScene.
std::optional<DeliveredFrame> TakeCompletedPpcFrame();
// Render-thread entry after the genuine USA Present capture. It consumes
// ONLY a complete original mailbox frame and passes authentic engine
// model/material/VS/PS witnesses to the existing direct Vulkan backend.
// No automatic takeover: caller must prove sole host presenter and camera.
bool ReplayNextVerifiedPpcFrame(
    std::span<const native::VerifiedXdkResourceWitness> witnesses,
    const native::FrameGeometry& output,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& shaders,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& gpu,
    uint64_t expected_original_frame_sequence=0,
    bool* reached_gpu=nullptr);
// A source-verified, immutable per-Present snapshot, built by the actual
// EoR material/mesh owner. No automatic XDK register->EoR ownership guesses.
// All host buffers/PSO/SRV references and their source owners must stay live
// until the GPU completion fence; a missing producer means NativeX fallback.
struct VerifiedWorkerEorFrame {
  // Immutable original USA Present sequence. Never replay resources or
  // materials from a different frame after an asynchronous GPU worker delay.
  uint64_t original_frame_sequence=0;
  native_x_renderer::Device* device=nullptr;
  native::FrameGeometry output{};
  std::vector<native::VerifiedXdkResourceWitness> witnesses;
  std::vector<std::shared_ptr<const model::UploadedOriginalModel>> residents;
  std::shared_ptr<const aot::ShaderBank> shaders;
  std::shared_ptr<const aot::EffectCatalog> effects;
  std::shared_ptr<const aot::ReflectionBank> reflection;
  std::shared_ptr<aot::ShaderBindingBackend> gpu;
};
// Register the REAL private USA offline ShaderEffects bank, effect/pass index,
// and reflection together. The game owns the source generations; the GPU
// worker holds shared immutable references, never detached naked pointers.
// This does NOT attest any engine Draw, model identity or camera.
bool InstallVerifiedSourceShaderPack(
    std::shared_ptr<const aot::ShaderBank> shaders,
    std::shared_ptr<const aot::EffectCatalog> effects,
    std::shared_ptr<const aot::ReflectionBank> reflection);
void ClearVerifiedSourceShaderPack() noexcept;
// Thread-safe source submission. A complete source-proven EoR engine owner
// must supply this per-frame; startup AOT shader banks alone are insufficient.
// The backend MUST use BindNativeEorVulkanBackbuffer, not Present itself.
bool InstallVerifiedWorkerEorFrame(
    std::shared_ptr<const VerifiedWorkerEorFrame> frame);
// A real original EoR Draw owner can now supply only its authenticated
// XDK resource witnesses and an existing NativeWorldStream::ResidentShared
// lookup: retain matching, fully uploaded Xbox model buffers from the
// same captured Present. No archive or shader source is guessed here.
// Invoke after USA Present capture, before the sole Vulkan worker's frame.
bool InstallVerifiedWorkerEorFrameWithResidents(
    std::shared_ptr<const VerifiedWorkerEorFrame> frame,
    const native::VerifiedOriginalResidentLookup& lookup);
void ClearVerifiedWorkerEorFrame() noexcept;
// Exact native EoR Vulkan *backbuffer completion*, not the guest XDK frame
// count and not proof the host GPU swapchain has finished Present.
struct EorWorkerStatus {
  uint64_t ready_backbuffers=0;
  uint64_t failed_after_gpu=0;
};
EorWorkerStatus GetEorWorkerStatus() noexcept;
native_x_renderer::HulkEorPresentResult TryWorkerEorFrame(
    native_x_renderer::Device& device) noexcept;
}
