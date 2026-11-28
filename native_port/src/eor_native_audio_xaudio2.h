#pragma once
// Native Windows audio output for already decoded PCM16 from original EoR
// assets. The 2008 FMOD FEV1 event table is metadata, not PCM: no event or
// XMA bank is silently treated as decoded sound. This backend owns real
// XAudio2 voices and keeps their sample bytes resident through playback.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include "eor_voice_options.h"
#include "eor_fmod_reverb_room.h"
#include "eor_fev_automation.h"
#include <optional>
#include <vector>

namespace hulk::eor {class Index;}
namespace hulk::eor::audio {
enum class Bus : uint8_t { Music=0, Sfx=1, Dialog=2 };
using VoiceId=uint64_t; // Zero never identifies a playing instance.
// EoR music and longer dialogue samples exceed one million PCM values.
// The original longest MPEG cue needs ~35 MiB decoded. Bound each
// voice to 48 MiB; the existing aggregate limit remains 96 MiB.
inline constexpr size_t kMaxNativePcmSamples=24u<<20;
inline constexpr bool ValidPcm16(std::span<const int16_t> pcm,
    uint32_t sample_rate,uint16_t channels) noexcept {
  return (channels==1u || channels==2u) &&
         sample_rate>=4000u && sample_rate<=96000u &&
         !pcm.empty() && pcm.size()<=kMaxNativePcmSamples &&
         pcm.size()%channels==0u;
}

struct EventCueRequest {
  uint32_t layer=0,instance=0,variant=0;
  float gain=1.f;
  VoiceOptions options{};
};
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
                 uint32_t sample_rate,uint16_t channels,float gain=1.f,VoiceId* started=nullptr,
                 VoiceOptions options={});
  // Real PCM16 sample from a validated original FMOD Ex FSB4 bank.
  // XMA/ADPCM/MPEG requires its own decoder and is never misplayed as PCM.
  bool PlayFsb4Pcm16(Bus bus,std::span<const std::byte> bank,
                     uint32_t sample_index,float gain=1.f);
  // Read WaveBanks through the selected original PC or USA Xbox 360
  // index and native ARC I/O; never cross a PC index with Xbox archive bytes.
  bool PlayIndexedWaveBankPcm16(Bus bus,const hulk::eor::Index& index,
      const std::filesystem::path& wavebanks_arc,uint32_t resource_hash,
      uint32_t sample_index,float gain=1.f);
  // Explicit source-proven event bank/sample route, not an invented FMOD callback.
  bool PlayProjectWaveSample(Bus bus,const hulk::eor::Index& index,
      const std::filesystem::path& project_archive,const std::filesystem::path& wave_archive,
      uint32_t project_hash,uint32_t bank_ordinal,uint32_t sample_ordinal,float gain=1.f,
      VoiceId* started=nullptr);
  // Independent event-instance controls. Stop destroys only the selected
  // voice; pause retains the original buffer and playback cursor.
  bool PlayProjectWaveform(Bus bus,const hulk::eor::Index& index,
      const std::filesystem::path& project_archive,const std::filesystem::path& wave_archive,
      uint32_t project_hash,size_t fev_offset,float gain=1.f,VoiceId* started=nullptr,
      VoiceOptions options={});
  // Play one explicitly selected original event-layer sound. The guest
  // event scheduler is responsible for timing, variants and DSP semantics.
  bool PlayEventVariant(Bus bus,const hulk::eor::Index& index,
      const std::filesystem::path& project_archive,const std::filesystem::path& wave_archive,
      uint32_t project_hash,std::string_view group,std::string_view event_name,
      uint32_t layer,uint32_t instance,uint32_t variant,float gain=1.f,VoiceId* started=nullptr,
      VoiceOptions options={});
  // Named-DSP route. Empty bindings select source FEV parameter ordinals
  // and source-frame volume/pitch/pan scheduling. Known finite seek/DSP
  // trajectories are rendered in event order. 3D needs actual spatial state.
  // The caller still supplies parameter snapshots; this is not a guest hook.
  bool PlayEventVariantWithFevDsp(Bus bus,const hulk::eor::Index& index,
      const std::filesystem::path& project_archive,const std::filesystem::path& wave_archive,
      uint32_t project_hash,std::string_view group,std::string_view event_name,
      uint32_t layer,uint32_t instance,uint32_t variant,
      std::span<const FevDspBinding> bindings,std::span<const FevParameterSnapshot> snapshots,
      float gain=1.f,VoiceId* started=nullptr,VoiceOptions options={});
  // An accepted delayed voice has an ID before playback begins. Call once
  // per elapsed game-time interval; pause freezes the pending clock.
  // Admit all explicitly selected cues as a group before starting any.
  // Returned IDs belong to the caller's verified event instance.
  std::optional<std::vector<VoiceId>> PlayEventGroup(Bus bus,const hulk::eor::Index& index,
      const std::filesystem::path& project_archive,const std::filesystem::path& wave_archive,
      uint32_t project_hash,std::string_view group,std::string_view event_name,
      std::span<const EventCueRequest> cues);
  size_t StopEventVoices(std::span<const VoiceId> voices) noexcept;
  size_t PauseEventVoices(std::span<const VoiceId> voices,bool pause) noexcept;
  // Caller must tick this on the game audio update path. Control changes
  // are applied at tick granularity, not guaranteed sample-accurate.
  bool AdvanceScheduledVoices(uint32_t elapsed_ms) noexcept;
  bool SetVoiceFrequencyRatio(VoiceId id,float ratio) noexcept;
  bool SetVoicePan(VoiceId id,float pan) noexcept;
  // Refresh a single-snapshot FEV voice without rebaking its DSP samples.
  bool SetVoiceLiveFevGainPitch(VoiceId id,float gain,float ratio) noexcept;
  bool SetVoiceLiveFevDsp(VoiceId id,const FevEventEntry& event,uint32_t layer,
      std::span<const float> values,bool spatial_ready) noexcept;
  bool SetVoiceSpatialState(VoiceId id,const SpatialVoiceState& spatial) noexcept;
  bool SetVoiceReverbSend(VoiceId id,float gain) noexcept;
  bool SetReverbRoom(const OriginalFmodReverbRoom& room) noexcept;
  // Replace the position and 2D/3D mix together, preserving the source voice.
  bool SetVoiceSpatialMix(VoiceId id,std::optional<SpatialVoiceState> spatial,
      std::optional<float> pan,std::optional<float> level) noexcept;
  // Explicit non-looping source position; keeps the event's VoiceId and PCM.
  bool SeekVoiceFrame(VoiceId id,uint32_t frame) noexcept;
  bool SetVoiceFilter(VoiceId id,VoiceFilter filter,float frequency,float one_over_q) noexcept;
  bool StopVoice(VoiceId id) noexcept;
  bool PauseVoice(VoiceId id,bool pause) noexcept;
  bool SetVoiceGain(VoiceId id,float gain) noexcept;
  bool SetBusVolume(Bus bus,float gain) noexcept;
  void SetMuted(bool muted) noexcept;
  void Update() noexcept;
  void StopAll() noexcept;
  size_t ActiveVoices() const noexcept;
 private:
  // Called only while the mixer mutex is held; no pruning or iterator invalidation.
  bool ReplaceVoiceAt(VoiceId id,uint32_t frame,std::optional<size_t> automation_block) noexcept;
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace hulk::eor::audio
