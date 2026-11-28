// Synthetic-only NativePC options write regression; no user TOML touched.
#include "../native_port/src/native_pc_options_store.h"
#include "../native_port/src/native_game_data_root_store.h"
#include "../native_port/src/native_pc_menu_actions.h"
#include "../native_port/src/native_pc_menu_rows.h"
#include "../native_port/src/eor_original_pc_menu.h"
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
#include <thread>

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
  Expect(Save(config, "hulk_eor_vulkan_replay", "true"),
         "Direct EoR Vulkan runtime selector was not persisted");
  Expect(Save(config, "hulk_eor_xdk_capture", "true"),
         "Read-only original XDK Draw source observer was not persisted");
  Expect(Save(config, "hulk_eor_xdk_source_mesh_capture", "true"),
         "Real source VB/IB capture selector was not persisted");
  Expect(Save(config, "native_x_renderer_resolution_scale", "1.0"),
         "Native render scale update failed");
  Expect(Save(config, "native_x_renderer_scaling", "nis"),
         "Native filter update failed");
  Expect(Save(config, "native_x_renderer_antialiasing", "off"),
         "Post-AA disable while retaining guest 4x MSAA failed");
  Expect(Save(config, "native_x_renderer_sharpness", "0.250000"),
         "Native sharpness update failed");
  Expect(Save(config, "hulk_original_havok_catchup", "true"),
         "Original fixed-step Havok option failed to persist");
  Expect(!Save(config, "hulk_original_havok_catchup", "60"),
         "Invalid Havok option value was accepted");
  Expect(Save(config, "hulk_guest_swap_fps_cap", "30"),
         "Native guest FPS cap update failed");

  Expect(Save(config, "hulk_bind_forward", "Up"), "Forward key rebinding failed");
  Expect(Save(config, "hulk_bind_primary", "MouseRight"),
         "Mouse attack rebinding failed");
  Expect(hulk::options::IsBindingOption("hulk_bind_forward") &&
         !hulk::options::IsBindingOption("hulk_bind_fake_action"),
         "Only source-supported native actions may be rebound");
  Expect(hulk::options::AnyBindingKey("Tab,  eScApE  ", hulk::options::IsEscapeKey) &&
         hulk::options::AnyBindingKey(" Esc, Space", hulk::options::IsEscapeKey) &&
         !hulk::options::AnyBindingKey("E, Escape2, ,", hulk::options::IsEscapeKey),
         "Escape aliases and multi-key overrides select the intended game action");
  Expect(hulk::options::IsBindingToken("MouseRight") &&
         !hulk::options::IsBindingToken("MouseWheelDown"),
         "Keyboard/mouse selector accepted an unsupported token");
  // Host-only W/H: change both TOML keys together, never overwrite
  // guest video_mode_width/height or xenos render-target descriptors.
  Expect(hulk::options::SaveWindowSize(config, 2560, 1600),
         "Native 16:10 window preset save failed");
  Expect(hulk::options::SaveWindowSize(config, 1234, 999),
         "Arbitrary valid custom monitor aspect should be saved");
  Expect(hulk::options::SaveWindowSize(config, 1080, 1920),
         "Portrait host resolution should be saved");
  Expect(hulk::options::SaveWindowSize(config, 7680, 4320),
         "Native 8K host resolution should be saved");
  Expect(hulk::options::SaveWindowSize(config, 2560, 1600),
         "Selecting a known resolution after custom edit should work");
  Expect(!hulk::options::SaveWindowSize(config, 0, 999) &&
         !hulk::options::SaveWindowSize(config, 1920, 0) &&
         !hulk::options::SaveWindowSize(config, 1920, 60) &&
         !hulk::options::SaveWindowSize(config, 16384, 16384) &&
         !hulk::options::SaveWindowSize(config, 400, 8000),
         "Unsafe zero, excessive memory or extreme aspect must be refused");
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
  Expect(changed.find("hulk_eor_vulkan_replay = true\r\n") != std::string::npos,
         "EoR worker selection was not persisted");
  Expect(changed.find("hulk_eor_xdk_capture = true\r\n") != std::string::npos &&
         changed.find("hulk_eor_xdk_source_mesh_capture = true\r\n") !=
             std::string::npos,
         "Original source observer/mesh capture was not persisted in one TOML");
  Expect(changed.find("hulk_original_havok_catchup = true") != std::string::npos,
         "Havok option missing in persisted TOML");
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
      std::pair{"hulk_eor_vulkan_replay", "auto"},
      std::pair{"hulk_eor_xdk_capture", "on"},
      std::pair{"hulk_eor_xdk_source_mesh_capture", "unbounded"},
      std::pair{"hulk_eor_vulkan_replay", "true\ninjected=true"},
      std::pair{"native_x_renderer_scaling", "smaa"},
      std::pair{"native_x_renderer_antialiasing", "dlss_quality"},
      std::pair{"native_x_renderer_antialiasing", "on\\ninjected=true"},
      std::pair{"native_x_renderer_sharpness", "nan"},
      std::pair{"native_x_renderer_sharpness", "0.8"},
      std::pair{"native_x_renderer_sharpness", "0x1p-2"},
      std::pair{"native_x_renderer_sharpness", ".5"},
      std::pair{"native_x_renderer_sharpness", "0."},
      std::pair{"native_x_renderer_sharpness", "00.5"},
      std::pair{"native_x_renderer_sharpness", "0,5"},
      std::pair{"hulk_mouse_horizontal_multiplier", "01.0"},
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
  Expect(proj_16_10->horizontal_focal_multiplier==1.0 &&
         std::abs(proj_16_10->vertical_focal_multiplier-0.9)<1e-8,
         "Real 16:10 must extend vertical FOV without cropping the sides");
  Expect(proj_ultra->horizontal_focal_multiplier < 1.0 &&
         proj_ultra->vertical_focal_multiplier==1.0 &&
         proj_4_3->horizontal_focal_multiplier==1.0 &&
         std::abs(proj_4_3->vertical_focal_multiplier-0.75)<1e-8 &&
         proj_4_3->vertical_fov_radians>proj_16_9->vertical_fov_radians,
         "Ultrawide must expand Hor+, 4:3 must expand Vert+ without cropping");
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
         std::abs(full_16_10.projection_x_multiplier - 1.0) < 1e-8 &&
         std::abs(full_16_10.projection_y_multiplier - 0.9) < 1e-8,
         "16:10 must extend vertical FOV with no side crop or stretched geometry");
  const auto full_16_9 = hulk::display::FullScreen3DContract(3840, 2160);
  Expect(full_16_9.valid &&
         std::abs(full_16_9.projection_x_multiplier - 1.0) < 1e-8,
         "Native 16:9 must not change 3D projection geometry");
  const auto full_ultra = hulk::display::FullScreen3DContract(3440, 1440);
  Expect(full_ultra.valid && full_ultra.host_width == 3440 &&
         full_ultra.host_height == 1440 &&
         full_ultra.projection_x_multiplier < 1.0 &&
         full_ultra.projection_y_multiplier == 1.0,
         "21:9 must extend horizontal FOV, not crop vertical scene");
  const auto full_4_3 = hulk::display::FullScreen3DContract(1024, 768);
  const auto full_32_9 = hulk::display::FullScreen3DContract(5120, 1440);
  const auto full_portrait=hulk::display::FullScreen3DContract(1080,1920);
  Expect(full_4_3.valid &&
         std::abs(full_4_3.projection_y_multiplier - 0.75)<1e-8 &&
         full_4_3.projection_x_multiplier==1.0 &&
         full_32_9.valid &&
         std::abs(full_32_9.projection_x_multiplier-0.5)<1e-8 &&
         full_portrait.valid &&
         full_portrait.projection_y_multiplier<0.35 &&
         full_portrait.projection_x_multiplier==1.0,
         "All supported aspect formats must expand the missing FOV axis");
  Expect(!hulk::display::FullScreen3DContract(0, 1080).valid &&
         !hulk::display::FullScreen3DContract(16384,16384).valid,
         "Invalid host pixel budget must not allocate GPU render targets");
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
  Write(config, "hulk_mouse_invert_x = true\nhulk_mouse_invert_y = true\nvsync = false\n");
  hulk::options::SetLiveConfigPath(config);
  Expect(hulk::options::SaveOriginalLookInversion(false,std::nullopt),
         "Original EoR yaw disabled option was not persisted");
  Expect(Read(config)=="hulk_mouse_invert_x = false\nhulk_mouse_invert_y = true\nvsync = false\n",
         "Original yaw update overwrote independent pitch or unrelated setting");
  Expect(hulk::options::SaveOriginalLookInversion(true,false),
         "Original EoR both-axis settings transaction failed");
  Expect(Read(config)=="hulk_mouse_invert_x = true\nhulk_mouse_invert_y = false\nvsync = false\n",
         "Original EoR both-axis settings did not survive transaction");
  for(unsigned attempt=0;attempt<100;++attempt) {
    Write(config,"hulk_mouse_invert_x = false\nhulk_mouse_invert_y = true\nvsync = false\n");
    bool original_ok=false,host_ok=false;
    std::thread original_menu([&]{original_ok=hulk::options::SaveOriginalLookInversion(true,false);});
    std::thread pc_menu([&]{host_ok=Save(config,"vsync","true");});
    original_menu.join();pc_menu.join();
    Expect(original_ok && host_ok,"Concurrent original and PC option writes failed");
    Expect(Read(config)=="hulk_mouse_invert_x = true\nhulk_mouse_invert_y = false\nvsync = true\n",
           "Concurrent menu writes lost a setting");
  }
  // Asset-folder selection is another writer of the very same TOML.
  // Verify cross-component concurrency, not just two calls to Save().
  for(unsigned attempt=0;attempt<100;++attempt) {
    Write(config,"game_data_root = \"old\"\nvsync = false\n");
    bool folder_ok=false,setting_ok=false;
    std::thread folder([&]{folder_ok=hulk::assets::config::SaveGameDataRoot(config,
        std::filesystem::path("C:/Games/Hulk"));});
    std::thread setting([&]{setting_ok=Save(config,"vsync","true");});
    folder.join();setting.join();
    Expect(folder_ok && setting_ok,"Concurrent asset root and setting writes failed");
    Expect(Read(config)=="game_data_root = \"C:/Games/Hulk\"\nvsync = true\n",
           "Asset root or setting lost during shared TOML transaction");
  }
  // The original EoR UI and ImGui can use the same settings transaction.
  // Rejected values must never reach the engine; restart-only values
  // must not invoke an unsafe live swapchain/device reconfiguration.
  unsigned live_calls=0;
  const auto apply=[&](std::string_view key,std::string_view value) {
    ++live_calls;
    Expect(key=="vsync" && value=="false","Unexpected live PC setting");
    return true;
  };
  const auto live_commit=hulk::options::CommitLivePcMenuValue("vsync","false",apply);
  Expect(live_commit.persisted && live_commit.live_applied && !live_commit.RestartRequired() &&
         live_calls==1,"Shared PC menu live setting was not applied");
  const auto deferred=hulk::options::CommitLivePcMenuValue("fullscreen","true",apply);
  Expect(deferred.persisted && !deferred.live_applied && deferred.RestartRequired() &&
         live_calls==1,"Restart-only setting reached live engine");
  const auto rejected=hulk::options::CommitLivePcMenuValue("vsync","invalid",apply);
  Expect(!rejected.persisted && !rejected.live_applied && !rejected.RestartRequired() &&
         live_calls==1,"Invalid setting reached live engine");
  Expect(hulk::options::SaveLiveWindowSize(2560,1600),"Shared native resolution save failed");
  const auto persisted=Read(config);
  Expect(persisted.find("window_width = 2560")!=std::string::npos &&
         persisted.find("window_height = 1600")!=std::string::npos &&
         persisted.find("fullscreen = true")!=std::string::npos &&
         persisted.find("vsync = false")!=std::string::npos,
         "Shared menu changes were not persisted");
  {
    using namespace hulk::eor::ui;
    OriginalPcStorageMenu menu;
    PcMenuVisual visual;
    auto paint=[&](const PcMenuVisual& frame){visual=frame;return true;};
    bool muted=false;
    auto read=[&](std::string_view key) -> std::string {
      if(key=="window_size")return "1920x1200";
      return key=="audio_mute" && muted?"true":"false";
    };
    auto action=[&](std::string_view key,std::string_view value){
      return hulk::options::CommitLivePcMenuValue(key,value,
          [&](std::string_view k,std::string_view v){
            Expect(k=="audio_mute","Restart-only setting applied while navigating");
            muted=v=="true";return true;
          });
    };
    auto event=[&](PcEvent ev){return menu.Handle(10,20,ev,true,paint,read,action);};
    Expect(menu.Enter(10,20,paint) && event(PcEvent::Select) && visual.title=="PC Settings" &&
           event(PcEvent::Select) && event(PcEvent::Down),
           "Original settings page did not select its resolution row");
    Expect(event(PcEvent::Increase) && visual.compact.find("2560x1600 (restart)")!=std::string::npos,
           "Original resolution action did not display its pending setting");
    const auto text=Read(config);
    Expect(text.find("window_width = 2560")!=std::string::npos &&
           text.find("window_height = 1600")!=std::string::npos &&
           text.find("window_size =")==std::string::npos,"Resolution transaction malformed");
    Expect(event(PcEvent::Back) && event(PcEvent::Down) && event(PcEvent::Select) &&
           visual.title.starts_with("Controls") &&
           event(PcEvent::Back) && event(PcEvent::Down) && event(PcEvent::Select) &&
           visual.title.starts_with("Audio") && event(PcEvent::Increase) && muted,
           "Original audio page failed to apply mute");
    Expect(event(PcEvent::Back) && visual.title=="PC Settings" &&
           event(PcEvent::Back) && visual.count==0,"Original settings page failed to return");
    unsigned exits=0;
    auto exit_event=[&](PcEvent ev,bool focused=true) {
      return menu.Handle(10,20,ev,focused,paint,read,action,[&]{++exits;return true;});
    };
    Expect(exit_event(PcEvent::Select) && visual.count==4 &&
           exit_event(PcEvent::Up) && exit_event(PcEvent::Select) &&
           visual.title.find("Exit to Windows?")!=std::string::npos && exits==0,
           "Original menu must display confirmation without exiting");
    Expect(exit_event(PcEvent::Back) && exits==0 && visual.title=="PC Settings",
           "Original exit cancellation failed");
    Expect(exit_event(PcEvent::Select) && exit_event(PcEvent::Select) &&
           exits==1 && !menu.Owns(10,20),
           "Original focused confirmation must request one native close");
  }
  hulk::options::PcMenuPageSession page;
  unsigned bind_rows=0;
  for(const auto& row:hulk::options::kPcMenuRows)
    if(row.kind==hulk::options::PcMenuRowKind::Binding) {
      ++bind_rows;
      Expect(hulk::options::IsBindingOption(row.key),"Unexpected menu binding");
    }
  Expect(bind_rows==21,"Menu binding coverage incomplete");
  page.Enter(10,1);
  Expect(!page.Select(10,2,"vsync","false",apply).persisted,
         "Stale page generation accepted");
  Expect(!page.Select(10,1,"vsync","invalid",apply).persisted,
         "Unknown menu choice accepted");
  Expect(page.Select(10,1,"vsync","false",apply).live_applied,
         "Native menu row was not applied");
  Expect(page.Resolution(10,1,2560,1600),"Native menu resolution rejected");
  Expect(page.Select(10,1,"hulk_eor_xdk_source_mesh_capture","true",apply).RestartRequired(),
         "Source mesh capture was not persisted as restart-only");
  const auto capture=Read(config);
  Expect(capture.find("hulk_eor_xdk_source_mesh_capture = true")!=std::string::npos &&
         capture.find("hulk_eor_xdk_capture = true")!=std::string::npos,
         "Source mesh capture lost its parent capture dependency");
  unsigned exits=0;
  Expect(!page.ConfirmExit(10,1,true,[&]{++exits;}),
         "Exit without confirmation request accepted");
  Expect(page.RequestExit(10,1),"Exit request failed");
  Expect(!page.Select(10,1,"vsync","false",apply).persisted,
         "Setting applied while exit confirmation pending");
  Expect(page.ConfirmExit(10,1,false,[&]{++exits;}) && exits==0,
         "Cancel exit closed game");
  Expect(page.RequestExit(10,1) &&
         page.ConfirmExit(10,1,true,[&]{++exits;}) && exits==1,
         "Confirmed exit not delivered exactly once");
  Expect(!page.Owns(10,1),"Closed page retained ownership");
  hulk::options::SetLiveConfigPath({});
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
