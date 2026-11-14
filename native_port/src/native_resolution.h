// Host pixel-resolution and safe aspect/letterbox preview for Hulk PC.
// No Xbox GPU scaling, ReXGlue or emulator dependencies. This models
// NativeXRenderer's default FIT (no guest camera widening) and is NOT a
// widescreen game-camera patch or proof of lossless post-processing.
#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <string_view>

namespace hulk::display {

struct WindowPreset {
  std::string_view label;
  uint32_t width, height;
};

inline constexpr std::array<WindowPreset, 11> kWindowPresets = {{
    {"1024x768 (4:3)", 1024, 768},
    {"1280x720 (16:9)", 1280, 720},
    {"1920x1080 (16:9)", 1920, 1080},
    {"2560x1440 (16:9)", 2560, 1440},
    {"3840x2160 (16:9)", 3840, 2160},
    {"1280x800 (16:10)", 1280, 800},
    {"1920x1200 (16:10)", 1920, 1200},
    {"2560x1600 (16:10)", 2560, 1600},
    {"3440x1440 (ultrawide)", 3440, 1440},
    {"5120x1440 (32:9)", 5120, 1440},
    {"2560x1080 (ultrawide)", 2560, 1080},
}};

inline bool IsWindowPreset(uint32_t width, uint32_t height) noexcept {
  for (const auto& preset : kWindowPresets)
    if (preset.width == width && preset.height == height) return true;
  return false;
}

// HULK/NATIVE-3D-PIXELS: a REAL nonuniform host raster target for
// a future validated USA camera-projection hook. Guest EDRAM layout,
// surfaces, memory addresses, tile pitch and physical dimensions are
// never rewritten. The viewport fills all swapchain pixels ONLY when
// the game's 3D camera matrix compensates horizontal focal length.
// Drawing with this raster contract before fixing the camera stretches
// character geometry and is intentionally NOT enabled yet.
struct FullViewResult {
  double scale_x = 0.0, scale_y = 0.0;
  double projection_x_multiplier = 0.0;
  uint32_t host_width = 0, host_height = 0;
  bool valid = false;
};

inline FullViewResult FullScreen3DContract(
    uint32_t output_w, uint32_t output_h,
    uint32_t guest_w = 1280, uint32_t guest_h = 720) noexcept {
  FullViewResult result;
  // NativeXRenderer cannot render a guest output below 1x without
  // an additional downscale stage; reject unsupported raster sizes.
  if (!output_w || !output_h || !guest_w || !guest_h ||
      output_w > 16384 || output_h > 16384 ||
      output_w < guest_w || output_h < guest_h)
    return result;
  result.scale_x = static_cast<double>(output_w) / guest_w;
  result.scale_y = static_cast<double>(output_h) / guest_h;
  result.projection_x_multiplier = result.scale_y / result.scale_x;
  result.host_width = output_w;
  result.host_height = output_h;
  result.valid = true;
  return result;
}

struct FitResult {
  uint32_t x = 0, y = 0;
  uint32_t width = 0, height = 0;
  // Host pixels per original guest 1280x720 pixel. The actual pipeline
  // may contain native or reduced intermediate resolves; test separately.
  double scale = 0.0;
  bool has_bars = false;
  bool valid = false;
};

inline FitResult FitOriginalPicture(uint32_t output_w, uint32_t output_h,
                                    uint32_t guest_w = 1280,
                                    uint32_t guest_h = 720) noexcept {
  FitResult r;
  if (output_w < 1 || output_h < 1 || guest_w < 1 || guest_h < 1 ||
      output_w > 16384 || output_h > 16384 ||
      guest_w > 16384 || guest_h > 16384)
    return r;
  // Mirrors native_x_renderer::renderer.cpp's FIT from the physical
  // swapchain, not Xenia's 720p presentation scaling. The guest output
  // dimensions remain fixed to keep render-target/XDK descriptors valid.
  const double horizontal = static_cast<double>(output_w) / guest_w;
  const double vertical = static_cast<double>(output_h) / guest_h;
  // NativeX does not render at less than 1 guest pixel per host pixel.
  // The presentation blit may downscale onto a *smaller* monitor, but
  // the actual 3D draw-target scale remains >= 1.
  const double fit = std::min(horizontal, vertical);
  r.scale = std::max(1.0, fit);
  r.width = std::min(output_w, static_cast<uint32_t>(guest_w * fit));
  r.height = std::min(output_h, static_cast<uint32_t>(guest_h * fit));
  r.x = (output_w - r.width) / 2;
  r.y = (output_h - r.height) / 2;
  r.has_bars = r.width != output_w || r.height != output_h;
  r.valid = r.width > 0 && r.height > 0;
  return r;
}
}  // namespace hulk::display
