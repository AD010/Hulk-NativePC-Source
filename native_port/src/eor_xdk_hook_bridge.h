#pragma once
#include "eor_xdk_draw_capture.h"
#include <rex/ppc/context.h>
#include <native_x_renderer/native_x_renderer.h>

namespace hulk::eor::xdk {
bool CaptureEnabled();
void CapturePpcDraw(Kind kind, uint32_t guest, const PPCContext& ctx,
                    uint8_t* guest_base);
void CapturePpcCommand(CommandKind kind, uint32_t guest,
                       const PPCContext& ctx);
void EndPpcHook(native_x_renderer::Call kind);
// The only completed read-only guest frame handoff to a source-verified
// native renderer. Callers must reject missing witnesses, missed frames,
// wrong resource ownership and incomplete shader bank BEFORE Vulkan BeginScene.
std::optional<DeliveredFrame> TakeCompletedPpcFrame();
}
