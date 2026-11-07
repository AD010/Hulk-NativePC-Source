// Runtime validated XDK profile for Hulk. No speculative offsets or calls.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <native_x_renderer/native_x_renderer.h>
namespace hulk::nx {
struct GameProfile {
  native_x_renderer::Profile profile{};
  std::string name{"The Incredible Hulk (2008)"};
  std::string version{"2.0.6534"};
  std::unordered_map<uint32_t, native_x_renderer::Call> calls;
  std::string error;
  bool experimental = false; // unverified layouts are explicitly rejected before native rendering
  void BindStrings() {profile.game=name.c_str(); profile.xdk_version=version.c_str();}
  GameProfile(){BindStrings();}
  GameProfile(const GameProfile&)=delete;
  GameProfile& operator=(const GameProfile&)=delete;
};
// Config schema in docs/HULK_NATIVE_GPU.md. Returns false with explicit error if incomplete.
bool LoadGameProfile(const std::filesystem::path& path, GameProfile& out);
// Exactly the same validation as the developer-only file loader; used for the
// profile embedded as RCDATA in the standalone Windows EXE (no extra INI).
bool LoadGameProfileFromText(std::string_view text, GameProfile& out);
// Set once on the UI thread, before the guest CPU starts.
void Install(const GameProfile* p);
native_x_renderer::Call Lookup(uint32_t guest);
// Generated from the same allowlist as the strong ReXGlue hooks.
// Runtime profile entries not compiled into the executable must fail closed.
bool IsCompiledHook(uint32_t guest);
} // namespace hulk::nx
