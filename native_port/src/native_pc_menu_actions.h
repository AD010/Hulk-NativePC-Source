#pragma once
// Shared PC menu command backend: independent of ImGui, VM and ReXGlue.
// A frontend supplies its authenticated user event and the real host setter.
// No guessed EoR object addresses, simulated menu events or UI rendering.
#include "native_pc_options_store.h"
#include "native_pc_bindings.h"
#include "native_audio_options.h"
#include "native_resolution.h"

namespace hulk::options {
struct PcMenuCommitResult {
  bool persisted=false;
  bool live_applied=false;
  bool RestartRequired() const noexcept {return persisted && !live_applied;}
};
inline bool IsLivePcMenuOption(std::string_view key) noexcept {
  return IsBindingOption(key) ||
      key=="hulk_mouse_invert_x" || key=="hulk_mouse_invert_y" ||
      key=="hulk_mouse_horizontal_multiplier" || key=="hulk_mouse_vertical_multiplier" ||
      key=="vsync" || key=="hulk_guest_swap_fps_cap" ||
      // The original scheduler reads this flag at each authenticated floor
      // callback and resets its thread-local debt when disabled.
      key=="hulk_original_havok_catchup" || IsLiveHostAudioOption(key);
}
template<class Persist,class Apply>
PcMenuCommitResult CommitPcMenuValue(std::string_view key,std::string_view value,
                                    Persist&& persist,Apply&& apply) {
  if(!persist(key,value))return {};
  // Persist first. A failed live host change leaves a restart-required
  // setting, never claims the live engine has accepted it.
  const bool applied=IsLivePcMenuOption(key) && apply(key,value);
  return {true,applied};
}
template<class Apply>
PcMenuCommitResult CommitLivePcMenuValue(std::string_view key,std::string_view value,Apply&& apply) {
  if(key=="window_size") {
    for(const auto& preset:display::kWindowPresets)
      if(value==preset.Dimensions())
        return {SaveLiveWindowSize(preset.width,preset.height),false};
    return {};
  }
  return CommitPcMenuValue(key,value,SaveLiveOption,apply);
}
} // namespace hulk::options
