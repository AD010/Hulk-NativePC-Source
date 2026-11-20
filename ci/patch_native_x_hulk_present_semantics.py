#!/usr/bin/env python3
"""Hulk NativeX: preserve working logical Swap frame-end, add front-buffer telemetry.

Log 079 confirms Swap at USA 0x82880778, mapped to native logical Present,
finished 31800 GPU frames but displayed black. Log 087 mapped the separate
outer guest Present to native Present: no finished frames, 256 MiB dynamic
heap exhaustion. The Hulk profile therefore maps Swap to NativeX Present
until a real guest frame boundary is verified. Preserve upstream
Present.r4 semantics: here r4 contains the inner Swap front-buffer object.

This pinned source edit ONLY adds sparse read-only Resolve lookup logging.
It does not modify input, shaders, guest XDK code or frame pacing.
"""
from pathlib import Path
import subprocess
import sys

import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
PRESENT_R4 = "  if (call == Call::kPresent) g_front_buffer = ctx.r4.u32;"
OLD = "  ComposePicture(front ? front->srv : nullptr, front ? front->width : 0, front ? front->height : 0, fetch1 & 0x3F,"
NEW = """  // If no matching guest Resolve exists, ComposePicture only clears black.
  // Sample once and every 600 presents, without logging resource contents.
  static uint64_t hulk_present_samples = 0;
  if (++hulk_present_samples == 1 || hulk_present_samples % 600 == 0) {
    REXLOG_INFO("[HULK/NATIVEX] present {} guest_front={:#x} has_fetch={} fetch1={:#x} "
                "resolved={} size={}x{}",
                hulk_present_samples, g_front_buffer, c.has_texture, fetch1,
                front != nullptr && front->srv != nullptr,
                front ? front->width : 0, front ? front->height : 0);
  }
""" + OLD


def patch_text(source: str) -> tuple[str, bool]:
    if source.count(PRESENT_R4) != 1:
        raise ValueError("NativeX Present r4 front setter missing, refusing zero-frame regression")
    if "if (call == Call::kSwap) {" in source:
        raise ValueError("Stale split Swap/Present patch detected")
    if source.count(NEW) == 1:
        return source, False
    if source.count(OLD) != 1:
        raise ValueError("Pinned NativeXRenderer ComposePicture anchor mismatch")
    return source.replace(OLD, NEW), True



# Only the pinned NativeX Vulkan worker owns Device/SwapChain. The Hulk
# original EoR renderer can replace ONE display frame there, not from PPC
# EndPpcHook or UI OnPostSetup. This opt-in callback is *not* registered until
# verified game meshes/materials/camera and an actual EoR backend exist.
# No callback: NativeX behavior is byte-for-byte unchanged.
def checked(source: str, before: str, after: str, name: str) -> tuple[str,bool]:
    if source.count(after) == 1:
        return source,False
    if source.count(before) != 1:
        raise ValueError("Pinned NativeX " + name + " source anchor changed")
    return source.replace(before,after),True


API_BEFORE = "namespace native_x_renderer {\n"
API_AFTER = API_BEFORE + """// A game-specific, opt-in full-original-scene compositor on the EXISTING
// NativeX Vulkan render worker. kNotReady MUST NOT touch any GPU state.
// kBackbufferReady already drew the entire host backbuffer but MUST NOT
// Present; the worker runs overlays and the only physical Present.
// kFailedAfterGpu never falls back to an unrelated Xenos/NativeX image.
class Device;
enum class HulkEorPresentResult : uint8_t {
  kNotReady, kBackbufferReady, kFailedAfterGpu
};
using HulkEorPresentCallback = HulkEorPresentResult (*)(Device&) noexcept;
void SetHulkEorPresentCallback(HulkEorPresentCallback callback) noexcept;

"""
INTERNAL_BEFORE = "void OnFrameEnd(uint32_t frame);\n"
INTERNAL_AFTER = INTERNAL_BEFORE + """// Used only from native_x_renderer::SetHulkEorPresentCallback.
void SetHulkEorPresentCallback(HulkEorPresentCallback callback) noexcept;
"""
DISPATCH_BEFORE = "bool Renders() { return g_renderer; }\n"
DISPATCH_AFTER = DISPATCH_BEFORE + """
void SetHulkEorPresentCallback(HulkEorPresentCallback callback) noexcept {
  renderer::SetHulkEorPresentCallback(callback);
}
"""
WORKER_DEVICE_BEFORE = "std::unique_ptr<Device> g_device;"
WORKER_DEVICE_AFTER = WORKER_DEVICE_BEFORE + """
// Written before guest rendering starts, read only by the Vulkan worker.
std::atomic<HulkEorPresentCallback> g_hulk_eor_present{nullptr};"""
WORKER_SETTER_BEFORE = "bool Initialize(const Profile& profile, rex::system::IGraphicsSystem* graphics, rex::ui::Window* window,"
WORKER_SETTER_AFTER = """void SetHulkEorPresentCallback(HulkEorPresentCallback callback) noexcept {
  g_hulk_eor_present.store(callback, std::memory_order_release);
}

""" + WORKER_SETTER_BEFORE
WORKER_PRESENT_BEFORE = "  FrameImage* image = with_swap ? NextFrameImage() : nullptr;"
WORKER_PRESENT_AFTER = """  // Exact source-proven EoR ownership can replace ONE native display
  // frame on this Vulkan worker. Never run the callback from PPC guest hooks,
  // with pending guest-swap frames or while frame generation is active.
  // The callback does not call Present: FinishPresent does that once here.
  if (!g_present_with_swaps && !g_frame_generation) {
    auto callback = g_hulk_eor_present.load(std::memory_order_acquire);
    if (callback) {
      const HulkEorPresentResult result = callback(*g_device);
      if (result != HulkEorPresentResult::kNotReady) {
        if (g_draw) g_draw->TargetsChanged();
        if (result == HulkEorPresentResult::kBackbufferReady) {
          // Exactly one host Present, plus the existing SDK diagnostics UI.
          auto* back = g_device->swap_chain()->GetCurrentBackBufferRTV();
          if (back) {
            g_overlay.Present(g_device->context(), back,
                              desc.Width, desc.Height, g_window);
            FinishPresent();
          } else {
            g_timer.End(g_device->context());
            g_device->FinishHiddenFrame();
            g_timer.Begin(g_device->context());
          }
        } else {
          // The EoR backend may have already touched Vulkan resources.
          // Never compose NativeX's PM4 front buffer as a fallback.
          g_timer.End(g_device->context());
          g_device->FinishHiddenFrame();
          g_timer.Begin(g_device->context());
        }
        return;
      }
    }
  }
""" + WORKER_PRESENT_BEFORE


def patch_worker_sources(contents: dict[str,str]) -> dict[str,str]:
    expected = {
        "include/native_x_renderer/native_x_renderer.h": [(API_BEFORE,API_AFTER)],
        "src/renderer.h": [(INTERNAL_BEFORE,INTERNAL_AFTER)],
        "src/native_x_renderer.cpp": [(DISPATCH_BEFORE,DISPATCH_AFTER)],
        "src/renderer.cpp": [
            ("#include <algorithm>\n", "#include <atomic>\n#include <algorithm>\n"),
            (WORKER_DEVICE_BEFORE,WORKER_DEVICE_AFTER),
            (WORKER_SETTER_BEFORE,WORKER_SETTER_AFTER),
            (WORKER_PRESENT_BEFORE,WORKER_PRESENT_AFTER),
        ],
    }
    # Validate ALL pinned anchors before writing a single upstream file.
    updated = dict(contents)
    for file, operations in expected.items():
        if file not in updated:
            raise ValueError("Missing pinned upstream NativeX file: " + file)
        for before, after in operations:
            updated[file], _ = checked(updated[file],before,after,file)
    return updated


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_present_semantics.py <NativeXRenderer checkout>")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN:
        raise ValueError(f"Unexpected NativeXRenderer revision {sha}, expected {PIN}")
    target = root / "src" / "renderer.cpp"
    updated, changed = patch_text(target.read_text(encoding="utf-8"))
    files = [
        "include/native_x_renderer/native_x_renderer.h",
        "src/renderer.h", "src/native_x_renderer.cpp", "src/renderer.cpp",
    ]
    originals = {name:(root/name).read_text(encoding="utf-8") for name in files}
    originals["src/renderer.cpp"] = updated
    patched = patch_worker_sources(originals)
    # Mutate only after every source file passed pinned revision and anchors.
    for name in files:
        if patched[name] != (root/name).read_text(encoding="utf-8"):
            (root/name).write_text(patched[name],encoding="utf-8",newline="\n")
    print("NativeX Hulk Swap frame-end:",
          "telemetry patched" if changed else "already patched",
          "; EoR Vulkan worker callback ready (unregistered until source proof)")


if __name__ == "__main__":
    main()
