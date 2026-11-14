// Exact Hulk NativePC keyboard/mouse binding tokens recognized by
// native_input_win.cpp::BindTokenDown. Standalone C++20, no RexGlue.
#pragma once

#include <array>
#include <string_view>

namespace hulk::options {

inline constexpr std::array<std::string_view, 50> kBindingTokens = {
    "A","B","C","D","E","F","G","H","I","J","K","L","M",
    "N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
    "0","1","2","3","4","5","6","7","8","9",
    "Space","Enter","Tab","Shift","Ctrl","Alt","Esc",
    "Up","Down","Left","Right",
    // Mirror native_input_win.cpp::BindTokenDown exactly.
    "MouseLeft","MouseRight","MouseMiddle"
};

// Only actual Xbox-to-PC input actions present in the current host
// bridge can be modified; no unknown TOML / engine variable may be
// invented from a menu label.
inline bool IsBindingOption(std::string_view key) noexcept {
  constexpr std::array<std::string_view, 21> actions = {
      "hulk_bind_forward", "hulk_bind_backward", "hulk_bind_left",
      "hulk_bind_right", "hulk_bind_menu_up", "hulk_bind_menu_down",
      "hulk_bind_menu_left", "hulk_bind_menu_right",
      "hulk_bind_confirm", "hulk_bind_back", "hulk_bind_primary",
      "hulk_bind_secondary", "hulk_bind_alt_back", "hulk_bind_lb",
      "hulk_bind_rb", "hulk_bind_lt", "hulk_bind_rt",
      "hulk_bind_start", "hulk_bind_select",
      "hulk_bind_rstick_click", "hulk_bind_walk",
  };
  for (auto candidate : actions) if (candidate == key) return true;
  return false;
}

inline bool IsBindingToken(std::string_view key) noexcept {
  if (key.empty()) return false;
  for (auto candidate : kBindingTokens)
    if (candidate == key) return true;
  return false;
}
}  // namespace hulk::options
