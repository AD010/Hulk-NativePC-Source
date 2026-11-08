
#include "native_x_hulk_profile.h"

#include <native_x_renderer/native_x_renderer.h>
#include <rex/logging.h>
#include <rex/cvar.h>
#include <rex/rex_app.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Independent GPU register observer can run while Xenos still draws a
// visible scene. This isolates XDK 6534 register/texture-state mismatches
// from native renderer output and never takes ownership of the swap chain.
REXCVAR_DEFINE_BOOL(hulk_native_x_verify_only, false, "Hulk",
                    "Use NativeX Trace/Observer while Xenos still renders (no NativeX Vulkan display)");

namespace hulk {
namespace {

std::unique_ptr<nx::GameProfile> g_profile;
bool g_initialized = false;

// Resource 3001 is compiled from native_port/hulk_native_x_profile.ini.
// The official standalone ZIP intentionally has no sidecar INI.
#ifdef _WIN32
std::string_view EmbeddedProfile() {
  const HMODULE module = GetModuleHandleW(nullptr);
  const HRSRC entry = module ? FindResourceW(module, MAKEINTRESOURCEW(3001), MAKEINTRESOURCEW(10)) : nullptr;
  if (!entry) return {};
  const DWORD size = SizeofResource(module, entry);
  const HGLOBAL resource = LoadResource(module, entry);
  const auto* data = resource ? static_cast<const char*>(LockResource(resource)) : nullptr;
  return data && size ? std::string_view(data, size) : std::string_view{};
}
#endif

bool ValidateProfile(const nx::GameProfile& profile) {
  if (profile.version != "2.0.6534") {
    REXLOG_ERROR(
        "[HULK/VULKAN] Unsupported XDK version: {}",
        profile.version);
    return false;
  }

  if (profile.calls.empty()) {
    REXLOG_ERROR(
        "[HULK/VULKAN] No guest GPU hooks configured");
    return false;
  }

  for (const auto& [guest, call] : profile.calls) {
    if (!nx::IsCompiledHook(guest)) {
      REXLOG_ERROR(
          "[HULK/VULKAN] Missing compiled hook "
          "0x{:08X}: {}",
          guest,
          native_x_renderer::CallName(call));
      return false;
    }
  }

  return true;
}

} // namespace

bool NativeXInitialize(
    rex::system::IGraphicsSystem* graphics,
    rex::ui::Window* window,
    const std::filesystem::path& cache) {

  if (g_initialized)
    return native_x_renderer::Renders();

  // A shipped release with an unverified 6534 GPU layout must not silently
  // take over the display or demand an experimental INI sidecar.
  // Keep renderer code compiled; opt in only when a validated profile exists.
  const bool verify_only = rex::cvar::Query<bool>("hulk_native_x_verify_only") &&
                           !rex::cvar::Query<bool>("native_x_renderer");
  if (!rex::cvar::Query<bool>("native_x_renderer") && !verify_only) {
    REXLOG_INFO("[HULK/GPU-MODE] Xenos selected; F11 inactive unless "
                "hulk_native_x_verify_only=true");
    return false;
  }
  REXLOG_INFO("[HULK/GPU-MODE] NativeX={} verify_only={}",
              rex::cvar::Query<bool>("native_x_renderer"), verify_only);

  if (!graphics || !window) {
    REXLOG_ERROR(
        "[HULK/VULKAN] Graphics system or window missing");
    return false;
  }

  auto candidate = std::make_unique<nx::GameProfile>();
  bool loaded = false;

  // Override only for developers analysing a specific legal XEX. The game
  // release uses its built-in, version-specific profile and needs no INI.
  if (const char* override_path = std::getenv("HULK_NATIVE_X_PROFILE");
      override_path && *override_path) {
    loaded = nx::LoadGameProfile(std::filesystem::path(override_path), *candidate);
  } else {
#ifdef _WIN32
    const auto profile_text = EmbeddedProfile();
    loaded = !profile_text.empty() &&
             nx::LoadGameProfileFromText(profile_text, *candidate);
    if (profile_text.empty()) candidate->error = "embedded RCDATA profile 3001 missing";
#else
    candidate->error = "embedded profile currently supported on Windows only";
#endif
  }

  if (!loaded) {
    REXLOG_ERROR(
        "[HULK/VULKAN] Profile validation failed; retaining Xenos fallback: {}",
        candidate->error);
    return false;
  }

  if (!ValidateProfile(*candidate))
    return false;

  std::error_code error;
  std::filesystem::create_directories(cache, error);

  if (error) {
    REXLOG_ERROR(
        "[HULK/VULKAN] Cache directory error: {}",
        error.message());
    return false;
  }

  // The profile and its string storage must remain alive
  // while renderer worker threads are active.
  g_profile = std::move(candidate);
  nx::Install(g_profile.get());

  // NativeXRenderer owns Vulkan instance/device/surface
  // initialization through the ReXGlue host window.
  // Do not create a competing VkSurfaceKHR here.
  native_x_renderer::Initialize(
      g_profile->profile,
      graphics,
      window,
      cache);

  // In observer-only mode NativeX's Vulkan device intentionally stays OFF.
  // Keep the PPC hooks and register tracker alive so the SDK Xenos GPU can
  // execute normal draws while Observer compares actual PM4 state.
  if (verify_only && !native_x_renderer::Renders()) {
    g_initialized = true;
    REXLOG_INFO("[HULK/VERIFY] Xenos visible, NativeX draw observer active; "
                "guest XDK={}, hook count={}; F9 trace, F11 verify",
                g_profile->version, g_profile->calls.size());
    return false;
  }

  if (!native_x_renderer::Renders()) {
    nx::Install(nullptr);
    g_profile.reset();

    REXLOG_ERROR(
        "[HULK/VULKAN] Native renderer initialization "
        "did not activate rendering");
    return false;
  }

  g_initialized = true;

  REXLOG_INFO(
      "[HULK/VULKAN] NativeXRenderer active; "
      "XDK={}, guest hooks={}",
      g_profile->version,
      g_profile->calls.size());

  if (g_profile->experimental) {
    REXLOG_INFO(
        "[HULK/VULKAN] Experimental XDK device layout; "
        "runtime correctness is not yet verified");
  }

  return true;
}

} // namespace hulk
