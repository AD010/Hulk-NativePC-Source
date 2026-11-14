// Synthetic-only NativePC options write regression; no user TOML touched.
#include "../native_port/src/native_pc_options_store.h"
#include "../native_port/src/native_pc_bindings.h"

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
      "native_x_renderer_scaling = \"bilinear\"\r\n"
      "native_x_renderer_sharpness = 0.0\r\n"
      "native_x_renderer_trace = false\r\n";
  Write(config, original);
  Expect(Save(config, "vsync", "false"), "Vsync TOML update failed");
  Expect(Save(config, "native_x_renderer_resolution_scale", "1.0"),
         "Native render scale update failed");
  Expect(Save(config, "native_x_renderer_scaling", "nis"),
         "Native filter update failed");
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
  const auto changed = Read(config);
  Expect(changed.find("game_data_root = \"C:/Private/USA\"\r\n") != std::string::npos,
         "User data path was lost");
  Expect(changed.find("native_x_renderer_trace = false\r\n") != std::string::npos,
         "Untouched options were changed");
  Expect(changed.find("native_x_renderer_scaling = \"nis\"\r\n") != std::string::npos,
         "Filter TOML string was not quoted");
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
  Write(config, "vsync = true\nvsync = false\n");
  Expect(!Save(config, "vsync", "true"), "Duplicate TOML keys must be refused");
  Expect(Read(config) == "vsync = true\nvsync = false\n",
         "Duplicate keys modified unexpectedly");
  Write(config, "game_data_root = \"\"\n");
  Expect(Save(config, "vsync", "true"),
         "Missing but known setting should be appended once");
  Expect(Read(config).find("vsync = true\n") != std::string::npos,
         "New setting was not appended");
  std::filesystem::remove(config);
  Expect(!Save(config, "vsync", "true"),
         "Do not create a second permanent TOML when the original is missing");
  return 0;
}
