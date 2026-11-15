// Synthetic-only NativePC options write regression; no user TOML touched.
#include "../native_port/src/native_pc_options_store.h"
#include "../native_port/src/native_pc_bindings.h"
#include "../native_port/src/native_resolution.h"
#include "../native_port/src/native_camera_projection.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
void Expect(bool yes, const char* error) {
  if (!yes) throw std::runtime_error(error);
}
std::string Read(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
void Write(const std::filesystem::path& p, const std::string& s) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f.write(s.data(), static_cast<std::streamsize>(s.size()));
  if (!f) throw std::runtime_error("Cannot write synthetic TOML");
}
} // namespace

int main() {
  using hulk::options::Save;
  const auto config = std::filesystem::temp_directory_path() /
                      "hulk-native-pc-settings-unit.toml";
  const auto sidecar = std::filesystem::path(config).concat(".nativepc-writing");
  const std::string original = "game_data_root = \"C:/Private/USA\"\r\n"
      "# preserve unrelated game graphics\r\n"
      "native_x_renderer_resolution_scale = 2.0\r\n"
      "vsync = true\r\n"
      "hulk_guest_swap_fps_cap = 0\r\n"
      "window_width = 1280\r\n"
      "window_height = 800\r\n"
      "video_mode_width = 1280\r\n"
      "video_mode_height = 720\r\n"
      "native_x_renderer_scaling = \"bilinear\"\r\n"
      "native_x_renderer_antialiasing = \"smaa\"\r\n"
      "native_x_renderer_sharpness = 0.0\r\n"
      "native_x_renderer_trace = false\r\n";
  Write(config, original);
  Expect(Save(config, "vsync", "false"), "Vsync TOML update failed");
  Expect(Save(config, "native_x_renderer_resolution_scale", "1.0"),
         "Native render scale update failed");
  Expect(Save(config, "native_x_renderer_scaling", "nis"),
         "Native filter update failed");
  Expect(Save(config, "native_x_renderer_antialiasing", "off"),
         "Post-AA disable while retaining guest 4x MSAA failed");
  Expect(Save(config, "native_x_renderer_sharpness", "0.250000"),
         "Native sharpness update failed");
  Expect(Save(config, "hulk_guest_swap_fps_cap", "30"),
         "Native guest FPS cap update failed");

  Expect(Save(config, "hulk_bind_forward", "Up"), "Forward key rebinding failed");
  Expect(Save(config, "hulk_bind_primary", "MouseRight"),
         "Mouse attack rebinding failed");
  Expect(hulk::options::IsBindingOption("hulk_bind_forward") &&
         !hulk::options::IsBindingOption("hulk_bind_fake_action"),
         "Only source-supported native actions may be rebound");
  Expect(hulk::options::IsBindingToken("MouseRight") &&
         !hulk::options::IsBindingToken("MouseWheelDown"),
         "Keyboard/mouse selector accepted an unsupported token");
  // Host-only W/H: change both TOML keys together, never overwrite
  // guest video_mode_width/height or xenos render-target descriptors.
  Expect(hulk::options::SaveWindowSize(config, 2560, 1600),
         "Native 16:10 window preset save failed");
  Expect(!hulk::options::SaveWindowSize(config, 1234, 999),
         "Unverified window resolution must be refused");
  Expect(!Save(config, "window_width", "1024"),
         "A single window-width update would split the W/H transaction");
  const auto changed = Read(config);
  Expect(changed.find("game_data_root = \"C:/Private/USA\"\r\n") != std::string::npos,
         "User data path was lost");
  Expect(changed.find("native_x_renderer_trace = false\r\n") != std::string::npos,
         "Untouched options were changed");
  Expect(changed.find("window_width = 2560\r\n") != std::string::npos &&
         changed.find("window_height = 1600\r\n") != std::string::npos,
         "The original host window dimensions were not updated together");
  Expect(changed.find("video_mode_width = 1280\r\n") != std::string::npos &&
         changed.find("video_mode_height = 720\r\n") != std::string::npos,
         "Saving host resolution damaged Xbox guest video-mode dimensions");
  Expect(changed.find("native_x_renderer_scaling = \"nis\"\r\n") != std::string::npos,
         "Filter TOML string was not quoted");
  Expect(changed.find("native_x_renderer_antialiasing = \"off\"\r\n") != std::string::npos,
         "Native AA TOML string was not quoted");
  Expect(changed.find("native_x_renderer_sharpness = 0.250000\r\n") != std::string::npos,
         "Sharpness number was changed");
  Expect(changed.find("vsync = false\r\n") != std::string::npos,
         "Vsync value did not persist");
  Expect(changed.find("hulk_guest_swap_fps_cap = 30\r\n") != std::string::npos,
         "FPS cap value did not persist");
  Expect(changed.find(R"toml(hulk_bind_forward = "Up")toml") != std::string::npos,
         "Forward key TOML string was not quoted and persisted");
  Expect(changed.find(R"toml(hulk_bind_primary = "MouseRight")toml") != std::string::npos,
         "Mouse attack TOML string was not quoted and persisted");
  Expect(changed.find("# preserve unrelated game graphics\r\n") != std::string::npos,
         "TOML comment was lost");
  Expect(!std::filesystem::exists(sidecar),
         "Failed to remove temporary, nonpermanent TOML file");

  for (auto [key, value] : {
      std::pair{"vsync", "maybe"},
      std::pair{"native_x_renderer_scaling", "smaa"},
      std::pair{"native_x_renderer_antialiasing", "dlss_quality"},
      std::pair{"native_x_renderer_antialiasing", "on\\ninjected=true"},
      std::pair{"native_x_renderer_sharpness", "nan"},
      std::pair{"native_x_renderer_sharpness", "0.8"},
      std::pair{"hulk_guest_swap_fps_cap", "-1"},
      std::pair{"game_data_root", "C:/untrusted"},
      std::pair{"hulk_bind_forward", "MouseWheelDown"},
      std::pair{"hulk_bind_fake_action", "W"},
      std::pair{"hulk_bind_primary", "A\\ninjected=true"},
      std::pair{"native_x_renderer_scaling", "nis\ninjected=true"},
  }) {
    Expect(!Save(config, key, value), "Unsafe setting should be rejected");
    Expect(Read(config) == changed, "Unsafe setting modified user TOML");
  }
  // This is the math for the ORIGINAL USA Hulk 3D camera hook, after its
  // PPC address, matrix layout and view lifetime are verified in Ghidra.
  // A host aspect is not a license to stretch UI, shadow or XDK surfaces.
  constexpr double guest_aspect = 16.0 / 9.0;
  const auto proj_16_9 = hulk::camera::AspectForView(
      1920.0, 1080.0, guest_aspect, 1.0);
  const auto proj_16_10 = hulk::camera::AspectForView(
      2560.0, 1600.0, guest_aspect, 1.0);
  const auto proj_ultra = hulk::camera::AspectForView(
      3440.0, 1440.0, guest_aspect, 1.0);
  const auto proj_4_3 = hulk::camera::AspectForView(
      1024.0, 768.0, guest_aspect, 1.0);
  Expect(proj_16_9.has_value() && proj_16_10.has_value() &&
         proj_ultra.has_value() && proj_4_3.has_value(),
         "Original 3D camera aspect math rejected legitimate resolutions");
  Expect(std::abs(proj_16_9->horizontal_focal_multiplier - 1.0) < 1e-8,
         "Original 16:9 camera must not be modified");
  Expect(std::abs(proj_16_10->horizontal_focal_multiplier - 10.0/9.0) < 1e-8,
         "Real 16:10 camera aspect ratio incorrect");
  Expect(proj_ultra->horizontal_focal_multiplier < 1.0 &&
         proj_4_3->horizontal_focal_multiplier > 1.0,
         "Ultrawide camera must widen; 4:3 must narrow horizontally");
  Expect(proj_ultra->horizontal_fov_radians >
         proj_16_10->horizontal_fov_radians,
         "Original vertical-FOV-preserving projection would crop ultrawide");
  Expect(!hulk::camera::AspectForView(2560.0, 0.0, guest_aspect, 1.0) &&
         !hulk::camera::AspectForView(2560.0, 1600.0, guest_aspect, 0.0),
         "Invalid projection FOV/framebuffer must fail closed");
  const auto full_16_10 = hulk::display::FullScreen3DContract(2560, 1600);
  Expect(full_16_10.valid && full_16_10.host_width == 2560 &&
         full_16_10.host_height == 1600 &&
         std::abs(full_16_10.scale_x - 2.0) < 1e-8 &&
         std::abs(full_16_10.scale_y - 20.0/9.0) < 1e-8 &&
         std::abs(full_16_10.projection_x_multiplier - 10.0/9.0) < 1e-8,
         "True 2560x1600 host 3D raster/projection compensation changed");
  const auto full_16_9 = hulk::display::FullScreen3DContract(3840, 2160);
  Expect(full_16_9.valid &&
         std::abs(full_16_9.projection_x_multiplier - 1.0) < 1e-8,
         "Native 16:9 must not change 3D projection geometry");
  const auto full_ultra = hulk::display::FullScreen3DContract(3440, 1440);
  Expect(full_ultra.valid && full_ultra.host_width == 3440 &&
         full_ultra.host_height == 1440 &&
         full_ultra.projection_x_multiplier < 1.0,
         "Full 21:9 target needs corresponding wider perspective");
  Expect(!hulk::display::FullScreen3DContract(1024, 768).valid &&
         !hulk::display::FullScreen3DContract(0, 1080).valid,
         "Unsupported sub-native output must not corrupt GPU surfaces");
  Expect(std::abs(full_16_10.projection_x_multiplier -
                  proj_16_10->horizontal_focal_multiplier) < 1e-8,
         "Native raster and camera projection contracts must agree");
  const auto fit_16_10 = hulk::display::FitOriginalPicture(2560, 1600);
  Expect(fit_16_10.valid && fit_16_10.width == 2560 &&
         fit_16_10.height == 1440 && fit_16_10.y == 80 &&
         fit_16_10.scale == 2.0 && fit_16_10.has_bars,
         "16:10 native 2560x1440 render/letterbox contract changed");
  const auto fit_16_9 = hulk::display::FitOriginalPicture(3840, 2160);
  Expect(fit_16_9.valid && fit_16_9.width == 3840 &&
         fit_16_9.height == 2160 && fit_16_9.scale == 3.0 &&
         !fit_16_9.has_bars,
         "4K native 3840x2160 render-size calculation changed");
  const auto fit_ultra = hulk::display::FitOriginalPicture(3440, 1440);
  Expect(fit_ultra.valid && fit_ultra.width == 2560 &&
         fit_ultra.height == 1440 && fit_ultra.x == 440 &&
         fit_ultra.scale == 2.0 && fit_ultra.has_bars,
         "Ultrawide should preserve safe game camera aspect until hook");
  const auto fit_small = hulk::display::FitOriginalPicture(1024, 768);
  Expect(fit_small.valid && fit_small.width == 1024 &&
         fit_small.height == 576 && fit_small.y == 96 &&
         fit_small.scale == 1.0,
         "Below-720p NativeX must not create sub-1x guest draw targets");
  Expect(!hulk::display::FitOriginalPicture(0, 720).valid,
         "Zero-resolution framebuffer must fail closed");
  Write(config, "vsync = true\nvsync = false\n");
  Expect(!Save(config, "vsync", "true"), "Duplicate TOML keys must be refused");
  Expect(Read(config) == "vsync = true\nvsync = false\n",
         "Duplicate keys modified unexpectedly");
  Write(config, "game_data_root = \"\"\n");
  Expect(Save(config, "vsync", "true"),
         "Missing but known setting should be appended once");
  Expect(Read(config).find("vsync = true\n") != std::string::npos,
         "New setting was not appended");
  // Native Windows editors often preserve UTF-8 BOM. Replacing the
  // first setting must keep the BOM without adding a second TOML key.
  Write(config, "\xEF\xBB\xBF" "vsync = true\r\n");
  Expect(Save(config, "vsync", "false"),
         "UTF-8 BOM-prefixed first key update failed");
  const std::string bom = Read(config);
  Expect(bom.size() >= 6 &&
         static_cast<unsigned char>(bom[0]) == 0xef &&
         static_cast<unsigned char>(bom[1]) == 0xbb &&
         static_cast<unsigned char>(bom[2]) == 0xbf,
         "UTF-8 BOM was discarded during NativePC options save");
  Expect(bom.find("vsync = false") == 3 &&
         bom.find("vsync =", 4) == std::string::npos,
         "BOM caused a second or stale first TOML setting");
  std::filesystem::remove(config);
  Expect(!Save(config, "vsync", "true"),
         "Do not create a second permanent TOML when the original is missing");
  return 0;
}
