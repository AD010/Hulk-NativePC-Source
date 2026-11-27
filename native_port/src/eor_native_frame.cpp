#include "eor_native_frame.h"
#include "native_resolution.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace hulk::eor::native {
namespace {
void Check(Size v) {
  if (!v.width || !v.height || v.width > 16384 || v.height > 16384)
    throw std::invalid_argument("Invalid host or game viewport size");
}
} // namespace

std::optional<HostRasterWindow> MapOriginalRasterWindow(
    const xdk::OriginalRasterWindow& v, Size guest, Rect dest) noexcept {
  if (!guest.width || !guest.height || guest.width>16384 || guest.height>16384 ||
      !dest.width || !dest.height || uint64_t(dest.left)+dest.width>16384 ||
      uint64_t(dest.top)+dest.height>16384) return std::nullopt;
  // Ordered comparisons reject NaNs. An unsupported off-target viewport
  // must not be clipped into a different projection.
  if (!(v.x>=0 && v.y>=0 && v.width>=0 && v.height>=0 &&
        double(v.x)+v.width<=guest.width && double(v.y)+v.height<=guest.height &&
        v.min_depth>=0 && v.min_depth<=1 && v.max_depth>=0 && v.max_depth<=1))
    return std::nullopt;
  const double sx=double(dest.width)/guest.width, sy=double(dest.height)/guest.height;
  HostRasterWindow out{float(dest.left+v.x*sx),float(dest.top+v.y*sy),
                       float(v.width*sx),float(v.height*sy),v.min_depth,v.max_depth,{}};
  // Source EoR viewports may have fractional edges (especially 2D text).
  // Truncating x/width separately can clip the left edge of a glyph or
  // lose the final column. Intersect the original signed XDK scissor in
  // guest space first, then round at the actual PHYSICAL host pixel centres.
  double x0=v.x,y0=v.y,x1=double(v.x)+v.width,y1=double(v.y)+v.height;
  if (v.scissor_enabled) {
    x0=std::clamp(double(v.scissor[0]),x0,x1);
    y0=std::clamp(double(v.scissor[1]),y0,y1);
    x1=std::clamp(double(v.scissor[2]),x0,x1);
    y1=std::clamp(double(v.scissor[3]),y0,y1);
  }
  const auto edge=[](double original,uint32_t src,uint32_t dst) {
    // First host pixel whose centre falls on or after this source edge.
    // Exact integer edges match the historical round-to-nearest mapping.
    return uint32_t(std::clamp(
        std::ceil(original*double(dst)/src-0.5),0.0,double(dst)));
  };
  const uint32_t left=edge(x0,guest.width,dest.width);
  const uint32_t top=edge(y0,guest.height,dest.height);
  out.scissor={dest.left+left,dest.top+top,
               edge(x1,guest.width,dest.width)-left,
               edge(y1,guest.height,dest.height)-top};
  if (v.width==0 || v.height==0) {
    // Vulkan requires nonzero viewport extent. The proven empty scissor
    // still executes the draw and covers zero fragments.
    out.x=float(dest.left); out.y=float(dest.top);
    out.width=float(dest.width); out.height=float(dest.height);
  }
  return out;
}

FrameGeometry PlanFrame(const FrameSetup& s) {
  Check(s.backbuffer);
  Check(s.original_guest);
  // A user-selected output is a native host pixel count. Enforce the same
  // safe device budget in the renderer as in the F10 settings writer, so
  // 8K/portrait/ultrawide all work without accepting impossible 16K^2 RTs.
  if(!display::IsSupportedHostSize(
        s.backbuffer.width,s.backbuffer.height))
    throw std::invalid_argument("Unsupported native host output dimensions");
  if (s.samples != 1 && s.samples != 2 && s.samples != 4 && s.samples != 8)
    throw std::invalid_argument("Unsupported native MSAA sample count");
  // The independent renderer always creates a full-host-resolution scene.
  // It must not letterbox the 16:9 Xbox image inside a 16:10/ultrawide host,
  // nor stretch the camera without the verified original USA projection ABI.
  // Same-as-source aspect needs no camera rewrite; every other aspect does.
  const bool wide = s.projection_hook_verified;
  const bool source_aspect_matches =
      uint64_t(s.backbuffer.width) * s.original_guest.height ==
      uint64_t(s.backbuffer.height) * s.original_guest.width;
  const Size world = s.backbuffer;
  const Rect rect{0, 0, world.width, world.height};
  uint32_t samples = s.samples;
  if (s.max_scene_color_samples) {
    const uint64_t pixels = uint64_t(world.width) * world.height;
    while (samples > 1 && pixels > s.max_scene_color_samples / samples)
      samples /= 2;
  }
  // A non-source aspect cannot become a deliverable native frame until
  // its source-proven USA camera matrices and screen-space passes are ready.
  // No 720p scaling, fake FOV, cropping or black-bar fallback.
  const bool draw_path_ready =
      s.engine_draw_hook_verified && s.offline_shader_cache_verified &&
      (wide || source_aspect_matches);
  return {world, s.backbuffer, rect, samples, wide, draw_path_ready,
          s.original_guest};
}

void ReplayNativeScene(const FrameGeometry& frame,
                       std::span<const IndexedDraw> draws, Backend& backend) {
  Check(frame.world);
  Check(frame.backbuffer);
  Check(frame.original_guest);
  if(!display::IsSupportedHostSize(
         frame.backbuffer.width,frame.backbuffer.height) ||
     uint64_t(frame.world.width)*frame.world.height>
          uint64_t(64)*1024*1024)
    throw std::invalid_argument("Unsafe native EoR viewport pixel budget");
  if (!frame.direct_engine_path_ready)
    throw std::logic_error("Native EoR draw and shader path not verified");
  if (draws.empty())
    throw std::logic_error("No real EoR draw calls captured");
  if (frame.samples != 1 && frame.samples != 2 &&
      frame.samples != 4 && frame.samples != 8)
    throw std::invalid_argument("Unsupported native MSAA sample count");
  if (!HasFullNativeCompositePixels(
          frame.world, frame.composite, frame.backbuffer))
    throw std::invalid_argument("Native EoR must fill the entire host viewport");
  if (!frame.widescreen_camera &&
      uint64_t(frame.backbuffer.width) * frame.original_guest.height !=
      uint64_t(frame.backbuffer.height) * frame.original_guest.width)
    throw std::invalid_argument("Unverified USA camera aspect conversion");
  // HULK/EOR-PREFLIGHT: validate the ENTIRE command batch before beginning
  // a host GPU scene. Previously, a bad draw late in a frame threw AFTER
  // BeginScene and earlier draws, leaving a partially submitted render pass
  // that could appear as flicker or poison the following frame.
  for (const auto& draw : draws) {
    if (!draw.vertex_buffer || !draw.index_buffer || !draw.vertex_shader ||
        (draw.original_vertex_only ? (draw.pixel_shader || !draw.source_frame) : !draw.pixel_shader) || !draw.index_count ||
        uint64_t(draw.first_index) + draw.index_count > UINT32_MAX)
      throw std::invalid_argument("Incomplete actual EoR geometry/shader submission");
  }
  // Guarantee all-or-fail host pass semantics even if shader binding,
  // Draw, same-size MSAA Resolve or output Composite fails or throws.
  struct FrameRollback {
    Backend& gpu;
    bool committed = false;
    ~FrameRollback() noexcept { if (!committed) gpu.AbortScene(); }
  } rollback{backend};
  if (!backend.BeginScene(frame.world, frame.samples))
    throw std::runtime_error("Cannot allocate native scene attachments");
  for (size_t batch_index=0;batch_index<draws.size();++batch_index) {
    const auto& draw=draws[batch_index];
    if (!backend.DrawIndexed(draw))
      throw std::runtime_error(
          "Native indexed draw submission failed at batch index " +
          std::to_string(batch_index) + " (source frame " +
          std::to_string(draw.source_frame) + ", ordinal " +
          std::to_string(draw.source_ordinal) + ")");
  }
  // Physical MSAA Resolve: target is the same pixel size as the world.
  // Do not blit to a 720p guest texture and then upscale the result.
  // Always FINISH the host-native scene: even MSAA=1 must unbind
  // color/depth render targets to make the scene SRV available for composite.
  // At >1 samples the same operation performs the physical MSAA resolve.
  if (!backend.ResolveScene(frame.world))
    throw std::runtime_error("Native scene finish / MSAA resolve failed");
  if (!backend.Composite(frame.composite, frame.backbuffer))
    throw std::runtime_error("Native scene composite failed");
  rollback.committed = true;
}
} // namespace hulk::eor::native
