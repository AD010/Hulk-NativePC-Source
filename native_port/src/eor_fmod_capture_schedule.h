#pragma once
#include <chrono>
#include <cstddef>
namespace hulk::eor::audio {
// The owning audio service serializes these calls. Diagnostic-only capture
// stays at 100 ms; verified wave starts wake the shadow on the next update.
class FmodCaptureSchedule {
 public:
  using Clock=std::chrono::steady_clock;
  void WaveStarted() noexcept {pending_=true;}
  void Reset() noexcept {pending_=false;sampled_=false;last_={};}
  bool Begin(Clock::time_point now,bool shadow) noexcept {
    const auto interval=shadow?std::chrono::milliseconds(16):std::chrono::milliseconds(100);
    if(sampled_&&!(shadow&&pending_)&&now>=last_&&now-last_<interval)return false;
    pending_=false;sampled_=true;last_=now;return true;
  }
  static constexpr size_t ShadowStartBudget() noexcept {return 4;}
 private:
  Clock::time_point last_{};
  bool pending_=false,sampled_=false;
};
} // namespace hulk::eor::audio
