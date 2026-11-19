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
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& gpu);
}
