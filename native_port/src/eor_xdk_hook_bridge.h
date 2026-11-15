#pragma once
#include "eor_xdk_draw_capture.h"
#include <rex/ppc/context.h>
#include <native_x_renderer/native_x_renderer.h>

namespace hulk::eor::xdk {
bool CaptureEnabled();
void CapturePpcDraw(Kind kind, uint32_t guest, const PPCContext& ctx);
void EndPpcHook(native_x_renderer::Call kind);
}
