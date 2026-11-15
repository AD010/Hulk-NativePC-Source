// Native Edge of Reality render submission contract. No virtual Xenos/PM4 API.
// This is a host-API-independent boundary for future engine-level draw hooks.
#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace hulk::eor::native {
struct Size { uint32_t width = 0, height = 0; };
struct Rect { uint32_t left = 0, top = 0, width = 0, height = 0; };

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
  Rect composite;                     // Host output rectangle, centered.
  uint32_t samples = 1;
  bool widescreen_camera = false;
  bool direct_engine_path_ready = false;
};

// Native mode becomes eligible only after the USA camera, native EoR draw
// source and offline shaders are separately verified. A 720p guest may still
// be used for game logic, but must never silently dictate host RT size.
FrameGeometry PlanFrame(const FrameSetup& setup);

struct IndexedDraw {
  uint64_t vertex_buffer = 0, index_buffer = 0;
  uint64_t vertex_shader = 0, pixel_shader = 0; // Offline cache IDs.
  uint32_t index_count = 0, first_index = 0;
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
