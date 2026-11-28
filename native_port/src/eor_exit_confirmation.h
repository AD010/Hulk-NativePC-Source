#pragma once
#include <cstdint>
namespace hulk::eor::ui {
enum class PcExitInput : uint8_t { None, Toggle, Select, Back };
enum class PcExitResult : uint8_t { Stay, Cancel, Confirm };
class PcExitConfirmation {
 public:
  constexpr void Reset() noexcept { yes_=false; }
  constexpr bool Yes() const noexcept { return yes_; }
  constexpr PcExitResult Handle(PcExitInput input) noexcept {
    switch(input) {
      case PcExitInput::Toggle: yes_=!yes_;return PcExitResult::Stay;
      case PcExitInput::Select: return yes_?PcExitResult::Confirm:PcExitResult::Cancel;
      case PcExitInput::Back: return PcExitResult::Cancel;
      default: return PcExitResult::Stay;
    }
  }
 private:
  bool yes_=false;
};
} // namespace hulk::eor::ui
