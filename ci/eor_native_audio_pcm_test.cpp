#include "../native_port/src/eor_native_audio_xaudio2.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace hulk::eor::audio;
int main() {
  constexpr std::array<int16_t,4> stereo{0,1000,-1000,0};
  constexpr std::array<int16_t,3> odd{0,1000,-1000};
  static_assert(ValidPcm16(stereo,48000,2));
  static_assert(ValidPcm16(stereo,44100,1));
  static_assert(!ValidPcm16(stereo,0,2));
  static_assert(!ValidPcm16(stereo,100000,2));
  static_assert(!ValidPcm16(stereo,48000,3));
  static_assert(!ValidPcm16(odd,48000,2));
  static_assert(!ValidPcm16(std::span<const int16_t>{},48000,1));
  assert(ValidPcm16(stereo,96000,2));
  std::cout << "Native PCM16 audio buffer contract PASS\n";
}
