// incredible_hulk - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>

#include "assets_locator.h"
#include "eor_spirv_bank.h"
#include "eor_xdk_hook_bridge.h"
#include "eor_effect_catalog.h"
#include "eor_shader_reflection.h"
#include "eor_original_shader_samplers.h"
#include "eor_original_virtual_shader_identity.h"
#include "native_pc_save_path.h"
#include "native_pc_save_format.h"
#include "native_gpu_autodiag.h"
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
    // HULK/PCMENU-CONFIG-LATE: F10 may already exist when this callback
    // runs. The dialog now holds a live reference to this resolved path.
    std::error_code config_error;
    REXLOG_INFO("[HULK/PCMENU-CONFIG] final_path_ready={} regular={}",
                !native_pc_config_.empty(),
                std::filesystem::is_regular_file(native_pc_config_, config_error));
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
    hulk::InstallAutomaticGpuDiagnostics();
    // Read-only verification of a real 2008 PC save file, if it exists.
    // Never overwrite it or reinterpret PC pointer-valued fields as Xbox.
    // This is not a guest save-load hook or PC-format conversion.
    if (auto root=hulk::save::WindowsSaveFolder(); root) {
      std::error_code ec;
      const auto retail=*root / "HULK.sav";
      if (std::filesystem::is_regular_file(retail,ec) && !ec) {
        try {
          const auto check=hulk::save::format::Read(
              retail,hulk::save::format::Kind::RetailPcLittleEndian);
          REXLOG_INFO("[HULK/RETAIL-PC-SAVE] valid original PC CRC32 "
                      "and {} indexed fields; inspection only, "
                      "guest save serializer unchanged",
                      check.sorted_field_count);
        } catch (const std::exception& error) {
          REXLOG_WARN("[HULK/RETAIL-PC-SAVE] existing PC save not imported "
                      "or modified: {}",error.what());
        }
      }
    }
    // Load only OWNER-generated original USA ShaderEffects inputs.
    // Three independent source artifacts are needed by ReplayAotScene:
    // SPIR-V source digests, actual effect/pass ownership, and full VS/PS
    // constant layouts. Loading one does NOT authorize rendering.
    // Existing private converters accept --private-output; the paths below
    // are the local cache destinations, never files published in source.
    if (runtime()) {
      const auto offline = runtime()->cache_root() / "eor_offline";
      const auto private_bank = offline / "hulk_shader_bank.bin";
      const auto private_effects = offline / "hulk_effect_ledger.bin";
      const auto private_reflection = offline / "hulk_shader_reflection.bin";
      const auto private_samplers = offline / "hulk_sampler_bank.bin";
      const auto private_virtual = offline / "hulk_shader_virtual_fingerprints.bin";
      std::error_code error;
      if (std::filesystem::is_regular_file(private_bank, error) && !error) {
        try {
          auto bank = std::make_shared<hulk::eor::aot::ShaderBank>();
          bank->Load(private_bank);
          const bool have_effects =
              std::filesystem::is_regular_file(private_effects, error) && !error;
          const bool have_reflection =
              std::filesystem::is_regular_file(private_reflection, error) && !error;
          if (have_effects && have_reflection) {
            auto effects = std::make_shared<hulk::eor::aot::EffectCatalog>();
            auto reflection = std::make_shared<hulk::eor::aot::ReflectionBank>();
            effects->Load(private_effects);
            reflection->Load(private_reflection);
            const bool have_samplers =
                std::filesystem::is_regular_file(private_samplers, error) &&
                !error;
            std::shared_ptr<hulk::eor::aot::OriginalXdkShaderSamplers>
                original_samplers;
            if(have_samplers) {
              original_samplers =
                  std::make_shared<hulk::eor::aot::OriginalXdkShaderSamplers>();
              original_samplers->Load(private_samplers);
              if(original_samplers->ShaderCount()!=bank->expected())
                throw std::invalid_argument(
                    "Original USA XDK sampler corpus does not match full "
                    "ShaderEffects source digest count");
            }
            // A partial compiled shader bank is useful for the FIRST
            // verified draw, but its ORIGINAL archive identities must
            // still equal both complete effect and reflection indexes.
            if (!effects->EffectCount() ||
                effects->ShaderCount() != bank->expected() ||
                reflection->size() != bank->expected())
              throw std::invalid_argument(
                  "Original USA EoR effect/shader/reflection identity mismatch");
            // Commit all verified dependencies together: no mixed generations
            // of private ShaderEffects are allowed in a live material pass.
            eor_effects_ = std::move(effects);
            eor_reflection_ = std::move(reflection);
            eor_samplers_ = std::move(original_samplers);
            const bool have_virtual =
                std::filesystem::is_regular_file(private_virtual,error) &&
                !error;
            if(have_virtual) {
              auto virtual_ids =
                  std::make_shared<hulk::eor::aot::OriginalVirtualShaderIdentity>();
              virtual_ids->Load(private_virtual);
              if(virtual_ids->size()!=bank->expected())
                throw std::invalid_argument(
                    "Original virtual shader identity bank does not match "
                    "complete USA ShaderEffects source digest count");
              eor_virtual_ids_ = std::move(virtual_ids);
              REXLOG_INFO("[HULK/EOR-ORIGINAL-VIRTUAL-ID] {} source keys "
                          "loaded; live guest XDK ShaderObject header "
                          "ownership not yet proven",
                          eor_virtual_ids_->size());
            }
            REXLOG_INFO("[HULK/EOR-XDK-SAMPLERS] original source shaders={} "
                        "complete={} (real XDK texture fetch owner still "
                        "requires exact engine Draw proof)",
                        eor_samplers_ ? eor_samplers_->ShaderCount() : 0u,
                        static_cast<bool>(eor_samplers_));
            REXLOG_INFO("[HULK/EOR-AOT] original pass catalog={} "
                        "shader identities={} reflection={} coherent; "
                        "live engine Draw ownership and Vulkan not enabled",
                        eor_effects_->EffectCount(), bank->expected(),
                        eor_reflection_->size());
          } else {
            REXLOG_INFO("[HULK/EOR-AOT] private effect ledger/reflection "
                        "absent; shader-only diagnostic, no native EoR Draw");
          }
          eor_aot_ = std::move(bank);
          REXLOG_INFO("[HULK/EOR-AOT] verified private SPIR-V blobs={}/{} "
                      "archive_coverage={} driver_PSO=false",
                      eor_aot_->size(), eor_aot_->expected(),
                      eor_aot_->HasFullArchiveCoverage());
        } catch (const std::exception& e) {
          // Atomic source generation: a broken new catalog must not leave
          // stale shader/reflection identities eligible for GPU replay.
          eor_aot_.reset();
          eor_effects_.reset();
          eor_reflection_.reset();
          eor_samplers_.reset();
          eor_virtual_ids_.reset();
          REXLOG_WARN("[HULK/EOR-AOT] rejected private EoR source pack: {}",
                      e.what());
        }
      }
      // An AOT shader compiler is not required just to identify the
      // original live USA XDK VS/PS ShaderObject. HEORVRT1 is an
      // independently SHA-verified private metadata index; permit the
      // read-only Xenos observer to use it without any HEORSPV1, PSO,
      // uploaded model, or guess about guest->archive material ownership.
      // The worker remains locked out until the full shader pack exists.
      if(!eor_virtual_ids_) {
        const auto private_virtual=offline/"hulk_shader_virtual_fingerprints.bin";
        std::error_code metadata_error;
        if(std::filesystem::is_regular_file(private_virtual,metadata_error) &&
           !metadata_error) {
          try {
            auto original =
                std::make_shared<hulk::eor::aot::OriginalVirtualShaderIdentity>();
            original->Load(private_virtual);
            if(original->size()!=5194u)
              throw std::invalid_argument(
                  "Incomplete USA HEORVRT1 original shader source census");
            eor_virtual_ids_=std::move(original);
            REXLOG_INFO("[HULK/EOR-OBSERVER] {} original USA Xenos virtual "
                        "shader identities available without SPIR-V; "
                        "no independent Vulkan Draw implied",
                        eor_virtual_ids_->size());
          } catch (const std::exception& e) {
            REXLOG_WARN("[HULK/EOR-OBSERVER] read-only private "
                        "ShaderContainer identities rejected: {}",e.what());
          }
        }
      }
      // A separate source-only HEORMAT2 catalog resolves the current
      // authentic VS+PS pair to its original ShaderEffects effect and
      // ordered pass, without requiring 5194 Vulkan modules to compile.
      // Material surfaces/texture GPU identity remain independently gated.
      if(eor_virtual_ids_ && !eor_effects_) {
        const auto original_effects=offline/"hulk_effect_ledger.bin";
        std::error_code ledger_error;
        if(std::filesystem::is_regular_file(original_effects,ledger_error) &&
           !ledger_error) {
          try {
            auto effects=std::make_shared<hulk::eor::aot::EffectCatalog>();
            effects->Load(original_effects);
            if(effects->EffectCount()!=233u ||
               effects->ShaderCount()!=eor_virtual_ids_->size())
              throw std::invalid_argument(
                  "USA ShaderEffects source-only original pass corpus incomplete");
            eor_effects_=std::move(effects);
            REXLOG_INFO("[HULK/EOR-OBSERVER] {} original effects ready "
                        "for live VS/PS -> effect/pass source recovery",
                        eor_effects_->EffectCount());
          } catch(const std::exception& e) {
            REXLOG_WARN("[HULK/EOR-OBSERVER] original "
                        "ShaderEffects effect catalog rejected: {}",e.what());
          }
        }
      }
    }
#if defined(HULK_NATIVE_X_LINKED)
    if(eor_virtual_ids_ &&
       !hulk::eor::xdk::InstallOriginalShaderCaptureIndex(
           eor_virtual_ids_,eor_effects_))
      REXLOG_WARN("[HULK/EOR-OBSERVER] USA original shader "
                  "capture index rejected at PPC Draw observer");
    // These are authentic private ShaderEffects inputs already verified
    // against matching source digest counts, not synthetic GPU material
    // bindings. Keep them resident for the sole Vulkan worker.
    if(eor_aot_ && eor_effects_ && eor_reflection_ &&
       !hulk::eor::xdk::InstallVerifiedSourceShaderPack(
           eor_aot_,eor_effects_,eor_reflection_,eor_samplers_,
           eor_virtual_ids_))
      REXLOG_WARN("[HULK/EOR-AOT] coherent private source pack rejected "
                  "at Vulkan worker handoff");
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
    REXLOG_INFO("[HULK/PCMENU] native full-screen game menu registered; Esc/F10, display, graphics, controls and confirmed exit");
  }
  void OnShutdown() override {
    // Stop new worker callbacks and discard frame ownership before the
    // original offline AOT sources are released; GPU fences remain owned by
    // the active renderer, not by the guest PPC mailbox.
#if defined(HULK_NATIVE_X_LINKED)
    native_x_renderer::SetHulkEorPresentCallback(nullptr);
    hulk::eor::xdk::ClearVerifiedWorkerEorFrame();
    hulk::eor::xdk::ClearVerifiedSourceShaderPack();
    hulk::eor::xdk::ClearOriginalShaderCaptureIndex();
#endif
    hulk::ReportAutomaticGpuDiagnostics();
    eor_reflection_.reset();
    eor_samplers_.reset();
    eor_virtual_ids_.reset();
    eor_effects_.reset();
    eor_aot_.reset();
    pc_menu_.reset();
  }
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
 private:
  std::shared_ptr<hulk::eor::aot::ShaderBank> eor_aot_;
  std::shared_ptr<hulk::eor::aot::EffectCatalog> eor_effects_;
  std::shared_ptr<hulk::eor::aot::ReflectionBank> eor_reflection_;
  std::shared_ptr<hulk::eor::aot::OriginalXdkShaderSamplers> eor_samplers_;
  std::shared_ptr<hulk::eor::aot::OriginalVirtualShaderIdentity> eor_virtual_ids_;
  std::filesystem::path native_pc_config_;
  std::unique_ptr<rex::ui::ImGuiDialog> pc_menu_;
};
