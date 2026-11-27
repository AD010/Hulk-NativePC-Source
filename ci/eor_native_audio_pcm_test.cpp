#include "../native_port/src/eor_native_audio_xaudio2.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
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
  // Valid longer original dialogue/music is not an artificial one-second
  // clip, while a single decoded allocation remains explicitly bounded.
  std::vector<int16_t> long_sample(2u<<20);
  assert(ValidPcm16(long_sample,48000,2));
#ifdef _WIN32
  // Link the actual Windows-only mixer implementation and its SDK imports.
  // Hosted CI may have no default audio endpoint; Ready() is not required.
  NativeXAudio2Mixer mixer;
  assert(mixer.ActiveVoices()==0u);
  mixer.StopAll();
#endif
  std::cout << "Native PCM16 audio buffer contract PASS\n";
}
