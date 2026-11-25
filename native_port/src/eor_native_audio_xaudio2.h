#pragma once
// Native Windows audio output for already decoded PCM16 from original EoR
// assets. The 2008 FMOD FEV1 event table is metadata, not PCM: no event or
// XMA bank is silently treated as decoded sound. This backend owns real
// XAudio2 voices and keeps their sample bytes resident through playback.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace hulk::eor::audio {
enum class Bus : uint8_t { Music=0, Sfx=1, Dialog=2 };
inline constexpr bool ValidPcm16(std::span<const int16_t> pcm,
    uint32_t sample_rate,uint16_t channels) noexcept {
  return (channels==1u || channels==2u) &&
         sample_rate>=8000u && sample_rate<=96000u &&
         !pcm.empty() && pcm.size()<=1048576u &&
         pcm.size()%channels==0u;
}

// This class is independent of the PPC guest runtime and GPU renderer.
// Feed it verified decoded PCM16; caller retains original FMOD event routing.
class NativeXAudio2Mixer {
 public:
  NativeXAudio2Mixer();
  ~NativeXAudio2Mixer() noexcept;
  NativeXAudio2Mixer(const NativeXAudio2Mixer&)=delete;
  NativeXAudio2Mixer& operator=(const NativeXAudio2Mixer&)=delete;

  bool Ready() const noexcept;
  bool PlayPcm16(Bus bus,std::span<const int16_t> samples,
                 uint32_t sample_rate,uint16_t channels,float gain=1.f);
  // Real PCM16 sample from a validated original FMOD Ex FSB4 bank.
  // XMA/ADPCM/MPEG requires its own decoder and is never misplayed as PCM.
  bool PlayFsb4Pcm16(Bus bus,std::span<const std::byte> bank,
                     uint32_t sample_index,float gain=1.f);
  bool SetBusVolume(Bus bus,float gain) noexcept;
  void SetMuted(bool muted) noexcept;
  void Update() noexcept;
  void StopAll() noexcept;
  size_t ActiveVoices() const noexcept;
 private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace hulk::eor::audio
