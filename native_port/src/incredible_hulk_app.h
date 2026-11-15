// incredible_hulk - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include "assets_locator.h"
#include "eor_spirv_bank.h"
#include "pc_menu.h"

#include <rex/rex_app.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

namespace hulk {
void InstallCrashReport();  // crash_report.cpp
#if defined(HULK_NATIVE_X_LINKED)
bool NativeXInitialize(rex::system::IGraphicsSystem* graphics, rex::ui::Window* window,
                       const std::filesystem::path& cache);
#endif
}  // namespace hulk

class IncredibleHulkApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<IncredibleHulkApp>(new IncredibleHulkApp(ctx, "incredible_hulk",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // ReXGlue's Xenos plugin supplies GPU command synchronization and fences.
  // When NativeXRenderer validates the Hulk profile, Vulkan owns host drawing.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
  }
  // This is a directly launchable Windows game, not a PowerShell project.
  // The one TOML is loaded in SetupEnvironment before OnFinalizePaths runs.
  // A user-specified game_data_root therefore overrides portable discovery.
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override {
    (void)resume;
    auto paths = defaults;
    // Use the actual resolved config path, not a guessed executable folder.
    native_pc_config_ = paths.config_path;
    // The SDK has loaded incredible_hulk.toml by this point. If its asset
    // path is absent or stale, Windows opens the native file picker once.
    const auto game = hulk::assets::LocateOrSelectGameData(
        paths.game_data_root, paths.config_path);
    if (!game) {
      // Canceling the system file picker exits normally, rather than showing
      // the misleading "--game_data_root was not provided" ReXGlue error.
      app_context().RequestDeferredQuit();
      return std::nullopt;
    }
    paths.game_data_root = *game;
    return paths;
  }
  // void OnLoadXexImage(std::string& xex_image) override {}
  // Developer aid (not in release builds): HULK_DUMP_IMAGE=<file> writes the loaded game image (from image_base) to that file, for
  // tools/find_missing_funcs.py, which looks for functions reached only through pointers.
  void OnPostLoadXexImage() override {
#if defined(HULK_DEV_TOOLS)
    const char* out = std::getenv("HULK_DUMP_IMAGE");
    if (!out || !*out) return;
    const uint8_t* image = runtime()->memory()->TranslateVirtual(PPCImageConfig.image_base);
    if (FILE* f = std::fopen(out, "wb")) {
      std::fwrite(image, 1, PPCImageConfig.image_size, f);
      std::fclose(f);
    }
#endif
  }
  // After the runtime's own exception handlers are in place (crash_report.cpp).
  void OnPostSetup() override {
    hulk::InstallCrashReport();
    // Load a PRIVATE owner-generated XenosRecomp+DXC bank, if it exists.
    // This validates bytecode and archive coverage but does not enable any
    // native EoR Vulkan pipelines until real shader/resource bindings exist.
    if (runtime()) {
      const auto private_bank = runtime()->cache_root() / "eor_offline" /
                                "hulk_shader_bank.bin";
      std::error_code error;
      if (std::filesystem::is_regular_file(private_bank, error) && !error) {
        try {
          eor_aot_ = std::make_unique<hulk::eor::aot::ShaderBank>();
          eor_aot_->Load(private_bank);
          REXLOG_INFO("[HULK/EOR-AOT] validated private SPIR-V blobs={}/{} "
                      "archive_coverage={} driver_PSO=false",
                      eor_aot_->size(), eor_aot_->expected(),
                      eor_aot_->HasFullArchiveCoverage());
        } catch (const std::exception& e) {
          eor_aot_.reset();
          REXLOG_WARN("[HULK/EOR-AOT] rejected corrupt/incompatible private "
                      "shader bank: {}", e.what());
        }
      }
    }
#if defined(HULK_NATIVE_X_LINKED)
    const bool vulkan_active = hulk::NativeXInitialize(
        runtime() ? runtime()->graphics_system() : nullptr,
        window(), runtime() ? runtime()->cache_root() : std::filesystem::path{});
    if (!vulkan_active) {
      if (const char* strict = std::getenv("HULK_REQUIRE_NATIVE_GPU");
          strict && strict[0] == '1') {
        // Native-only launches fail instead of silently reverting to EDRAM/Xenos.
        std::exit(EXIT_FAILURE);
      }
    }
#endif
  }
  // SDL3 emits window focus/minimized/restored events, but upstream ReXApp's
  // default lifecycle hooks are empty. Explicitly request a new frame after
  // Alt+Tab so an exposed window doesn't remain stuck showing the prior image.
  // This does NOT claim recovery from Vulkan DEVICE_LOST or a dead guest thread.
  void OnWindowFocusChanged(bool focused) override {
    REXLOG_INFO("[HULK/WINDOW] focus={}", focused ? "gained" : "lost");
    if (focused && window()) window()->RequestPaint();
  }
  void OnWindowMinimized() override {
    REXLOG_INFO("[HULK/WINDOW] minimized");
  }
  void OnWindowRestored() override {
    REXLOG_INFO("[HULK/WINDOW] restored; requesting host repaint");
    if (window()) window()->RequestPaint();
  }
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    pc_menu_ = hulk::CreatePcMenu(drawer, window(), native_pc_config_, [this] {
      if (window()) window()->RequestClose();
    });
    REXLOG_INFO("[HULK/PCMENU] NativePC UI registered; F10 opens controls and Exit");
  }
  void OnShutdown() override { eor_aot_.reset(); pc_menu_.reset(); }
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
 private:
  std::unique_ptr<hulk::eor::aot::ShaderBank> eor_aot_;
  std::filesystem::path native_pc_config_;
  std::unique_ptr<rex::ui::ImGuiDialog> pc_menu_;
};
