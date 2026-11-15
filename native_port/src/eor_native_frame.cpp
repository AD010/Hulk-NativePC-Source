#include "eor_native_frame.h"
#include <cstdint>
#include <stdexcept>

namespace hulk::eor::native {
namespace {
void Check(Size v) {
  if (!v.width || !v.height || v.width > 16384 || v.height > 16384)
    throw std::invalid_argument("Invalid host or game viewport size");
}
} // namespace

FrameGeometry PlanFrame(const FrameSetup& s) {
  Check(s.backbuffer);
  Check(s.original_guest);
  if (s.samples != 1 && s.samples != 2 && s.samples != 4 && s.samples != 8)
    throw std::invalid_argument("Unsupported native MSAA sample count");
  // Never claim a wider camera unless its real USA projection update is
  // verified; letterbox the native-rasterized 16:9 scene in that case.
  const bool wide = s.projection_hook_verified;
  Size world = s.backbuffer;
  Rect rect{0, 0, world.width, world.height};
  if (!wide) {
    const uint64_t x = uint64_t(s.backbuffer.height) * s.original_guest.width;
    const uint64_t y = uint64_t(s.backbuffer.width) * s.original_guest.height;
    if (x > y) {
      world.height = uint32_t(y / s.original_guest.width);
    } else {
      world.width = uint32_t(x / s.original_guest.height);
    }
    if (!world.width || !world.height)
      throw std::invalid_argument("Guest aspect cannot fit host surface");
    rect = {(s.backbuffer.width - world.width) / 2u,
            (s.backbuffer.height - world.height) / 2u,
            world.width, world.height};
  }
  uint32_t samples = s.samples;
  if (s.max_scene_color_samples) {
    const uint64_t pixels = uint64_t(world.width) * world.height;
    while (samples > 1 && pixels > s.max_scene_color_samples / samples)
      samples /= 2;
  }
  return {world, s.backbuffer, rect, samples, wide,
          s.engine_draw_hook_verified && s.offline_shader_cache_verified &&
          s.projection_hook_verified};
}

void ReplayNativeScene(const FrameGeometry& frame,
                       std::span<const IndexedDraw> draws, Backend& backend) {
  Check(frame.world);
  Check(frame.backbuffer);
  if (!frame.direct_engine_path_ready)
    throw std::logic_error("Native EoR draw, shader and camera path not verified");
  if (draws.empty())
    throw std::logic_error("No real EoR draw calls captured");
  if (frame.composite.left > frame.backbuffer.width ||
      frame.composite.top > frame.backbuffer.height ||
      frame.composite.width != frame.world.width ||
      frame.composite.height != frame.world.height ||
      uint64_t(frame.composite.left) + frame.composite.width > frame.backbuffer.width ||
      uint64_t(frame.composite.top) + frame.composite.height > frame.backbuffer.height)
    throw std::invalid_argument("Invalid host-native composite bounds");
  if (!backend.BeginScene(frame.world, frame.samples))
    throw std::runtime_error("Cannot allocate native scene attachments");
  for (const auto& draw : draws) {
    if (!draw.vertex_buffer || !draw.index_buffer || !draw.vertex_shader ||
        !draw.pixel_shader || !draw.index_count ||
        uint64_t(draw.first_index) + draw.index_count > UINT32_MAX)
      throw std::invalid_argument("Incomplete actual EoR geometry/shader submission");
    if (!backend.DrawIndexed(draw))
      throw std::runtime_error("Native indexed draw submission failed");
  }
  // Physical MSAA Resolve: target is the same pixel size as the world.
  // Do not blit to a 720p guest texture and then upscale the result.
  if (frame.samples > 1 && !backend.ResolveScene(frame.world))
    throw std::runtime_error("Native same-extent MSAA resolve failed");
  if (!backend.Composite(frame.composite, frame.backbuffer))
    throw std::runtime_error("Native scene composite failed");
}
} // namespace hulk::eor::native
