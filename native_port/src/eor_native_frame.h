// Native Edge of Reality render submission contract. No virtual Xenos/PM4 API.
// This is a host-API-independent boundary for future engine-level draw hooks.
#pragma once
#include "eor_xdk_guest_constants.h"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace hulk::eor::native {
struct Size { uint32_t width = 0, height = 0; };
struct Rect { uint32_t left = 0, top = 0, width = 0, height = 0; };

struct HostRasterWindow {
  float x, y, width, height, min_depth, max_depth;
  Rect scissor;
};
std::optional<HostRasterWindow> MapOriginalRasterWindow(
    const xdk::OriginalRasterWindow& source, Size guest, Rect destination) noexcept;

// A native EoR frame must reach the host swapchain at its REAL scene
// pixel density. The original 1280x720 guest surface cannot silently
// become a 2560x1440/2560x1600 host scene through a blit or stretch.
// Exact pixel-copy verification is separate from the production requirement
// that the entire host viewport be native 3D pixels (no letterbox fallback).
constexpr bool HasExactNativeCompositePixels(
    Size source, Rect destination, Size host) noexcept {
  return source.width && source.height &&
         source.width == destination.width &&
         source.height == destination.height &&
         host.width && host.height &&
         uint64_t(destination.left) + destination.width <= host.width &&
         uint64_t(destination.top) + destination.height <= host.height;
}

// Only full-viewport, one-source-pixel-per-host-pixel frames may reach
// the independent Hulk presentation path. A valid smaller rectangle is
// diagnostic geometry, NOT a deliverable native Windows render mode.
constexpr bool HasFullNativeCompositePixels(
    Size source, Rect destination, Size host) noexcept {
  return HasExactNativeCompositePixels(source,destination,host) &&
         destination.left==0 && destination.top==0 &&
         destination.width==host.width &&
         destination.height==host.height;
}

struct FrameSetup {
  Size backbuffer;                    // Actual host swapchain extent.
  Size original_guest{1280, 720};   // Historical layout only, NEVER a host RT.
  bool projection_hook_verified = false;
  bool engine_draw_hook_verified = false;
  bool offline_shader_cache_verified = false;
  uint32_t samples = 1;
  // Optional host resource budget. Physical world width/height NEVER shrink.
  // Counts only color MSAA samples, not depth, shadows or VRAM allocation.
  // 0 means no cap; this is not a GPU utilization or frame-time model.
  uint64_t max_scene_color_samples = 0;
};
struct FrameGeometry {
  Size world;                         // Physically rasterized scene pixels.
  Size backbuffer;
  Rect composite;                     // Always full host output, no bars.
  uint32_t samples = 1;
  bool widescreen_camera = false; // Actual USA camera ABI proven for wider 3D
  bool direct_engine_path_ready = false; // Original engine Draw + AOT shaders proven
  // Original screenshot/HUD design resolution; independent of native
  // world/RT pixel density, used only for VERIFIED 2D overlay viewport.
  Size original_guest{1280,720};
};

// Native Draw requires verified original EoR engine calls and offline
// shader provenance. Non-source aspects additionally require a verified USA
// 3D camera rewrite; without it the output is NOT ready, rather than
// letterboxing or stretching the original Xbox viewport. All output
// attachments always match the full user-selected physical host resolution.
FrameGeometry PlanFrame(const FrameSetup& setup);

struct IndexedDraw {
  uint64_t vertex_buffer = 0, index_buffer = 0;
  uint64_t vertex_shader = 0, pixel_shader = 0; // Offline cache IDs.
  uint32_t index_count = 0, first_index = 0;
  // Signed host vertex offset after verified guest->native rebase.
  int32_t base_vertex = 0;
  // Exact original frame and captured call ordinal. A model may be
  // rendered hundreds of times with IDENTICAL buffers/shaders but a
  // different per-instance World or ViewProj each time.
  uint64_t source_frame = 0;
  uint32_t source_ordinal = UINT32_MAX;
  // Original immutable guest VS constant snapshot captured at this exact
  // indexed Draw, for source-correct World/ViewProj per object instance.
  std::optional<xdk::OriginalVsMatrices> guest_vs_matrices;
  // Complete immutable VS/PS float4 banks captured on the SAME PPC Draw.
  // Shared through native staging without copying 8 KiB per handoff.
  std::shared_ptr<const xdk::OriginalShaderRegisters> guest_shader_constants;
  bool original_vertex_only=false;
};

// All methods operate on host-native resources and draw calls; no GPU
// register snapshots, EDRAM keys, PM4 packets, or forced 720p resolves.
class Backend {
 public:
  virtual ~Backend() = default;
  virtual bool BeginScene(Size physical, uint32_t samples) = 0;
  // Revert pending GPU writes and transient attachments on any pass failure;
  // must be safe after a failed BeginScene and must never throw.
  virtual void AbortScene() noexcept = 0;
  virtual bool DrawIndexed(const IndexedDraw& draw) = 0;
  virtual bool ResolveScene(Size physical) = 0;
  virtual bool Composite(Rect destination, Size backbuffer) = 0;
};

// Does not fabricate EoR commands. An empty draw list is NOT a rendered frame.
// Throws on missing shader/geometry IDs or invalid pass states.
void ReplayNativeScene(const FrameGeometry& frame,
                       std::span<const IndexedDraw> draws, Backend& backend);
} // namespace hulk::eor::native
