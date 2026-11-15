// Read-only connection to REAL Hulk USA XDK Draw and Present wrappers.
// Never writes guest memory, never bypasses the original NativeX or Xenos.
// This is an input provenance oracle, not a completed Vulkan renderer.
#include "eor_xdk_hook_bridge.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <mutex>

REXCVAR_DEFINE_BOOL(hulk_eor_xdk_capture, false, "Hulk/Diagnostics",
                    "Opt-in original XDK Draw/Present call snapshot; no rendering redirection");
namespace hulk::eor::xdk {
namespace {
std::mutex g_mutex;
FrameCapture g_collector;
}
bool CaptureEnabled() { return REXCVAR_GET(hulk_eor_xdk_capture); }
void CapturePpcDraw(Kind kind, uint32_t guest, const PPCContext& ctx) {
  if (!CaptureEnabled()) return;
  const RawDraw draw{
      kind, guest, static_cast<uint32_t>(ctx.lr),
      {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,
       ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32}};
  std::lock_guard lock(g_mutex);
  g_collector.Record(draw);
}
void EndPpcHook(native_x_renderer::Call kind) {
  if (kind != native_x_renderer::Call::kPresent || !CaptureEnabled()) return;
  Frame frame;
  {
    std::lock_guard lock(g_mutex);
    frame = g_collector.Finish();
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
    REXLOG_INFO("[HULK/EOR-XDK-PROVENANCE] frame={} actual XDK calls: "
                "indexed={} vertices={} UP={} dropped={}; "
                "native EoR vertex/index/shader identity NOT decoded",
                frame.sequence, indexed, verts, up, frame.dropped);
  }
}
}  // namespace hulk::eor::xdk
