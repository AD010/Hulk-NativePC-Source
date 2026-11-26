#include "eor_native_audio_xaudio2.h"
#include "eor_fsb4_pcm.h"
#include "eor_archive.h"
#ifdef _WIN32
#include <xaudio2.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace hulk::eor::audio {
namespace {
unsigned BusIndex(Bus bus) noexcept {return static_cast<unsigned>(bus);}
bool ValidGain(float gain) noexcept {
  return std::isfinite(gain) && gain>=0.f && gain<=1.f;
}
}
struct NativeXAudio2Mixer::State {
  struct Voice {
    IXAudio2SourceVoice* source=nullptr;
    std::vector<int16_t> samples;
    unsigned bus=0;
    float gain=1.f;
    ~Voice() noexcept {
      if(source) {
        source->Stop(0);
        source->FlushSourceBuffers();
        source->DestroyVoice();
      }
    }
  };
  IXAudio2* engine=nullptr;
  IXAudio2MasteringVoice* master=nullptr;
  bool com_owned=false;
  DWORD owner_thread_id=0;
  std::vector<std::unique_ptr<Voice>> voices;
  std::array<float,3> bus_volumes{1.f,1.f,1.f};
  bool muted=false;
  ~State() noexcept {
    voices.clear();
    if(master)master->DestroyVoice();
    if(engine)engine->Release();
    // XAudio2's COM initialization is paired on the owning game thread.
    // The mixer is a single-thread-owner audio service, not an apartment-
    // transferable pointer.
    if(com_owned && GetCurrentThreadId()==owner_thread_id)
      CoUninitialize();
  }
  void UpdateVolume(Voice& voice) noexcept {
    if(voice.source)
      (void)voice.source->SetVolume(muted?0.f:
          voice.gain*bus_volumes[voice.bus],XAUDIO2_COMMIT_NOW);
  }
};
NativeXAudio2Mixer::NativeXAudio2Mixer() {
  auto next=std::make_unique<State>();
  next->owner_thread_id=GetCurrentThreadId();
  const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  if(SUCCEEDED(hr))next->com_owned=true;
  else if(hr!=RPC_E_CHANGED_MODE)return; // another existing COM mode
  if(FAILED(XAudio2Create(&next->engine,0,XAUDIO2_DEFAULT_PROCESSOR)) ||
     !next->engine ||
     FAILED(next->engine->CreateMasteringVoice(&next->master)) ||
     !next->master)
    return;
  state_=std::move(next);
}
NativeXAudio2Mixer::~NativeXAudio2Mixer() noexcept =default;
bool NativeXAudio2Mixer::Ready() const noexcept {
  return state_ && state_->engine && state_->master;
}
void NativeXAudio2Mixer::Update() noexcept {
  if(!Ready())return;
  auto& v=state_->voices;
  v.erase(std::remove_if(v.begin(),v.end(),[](const auto& voice) {
    if(!voice || !voice->source)return true;
    XAUDIO2_VOICE_STATE status{};
    voice->source->GetState(&status,XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return status.BuffersQueued==0;
  }),v.end());
}
bool NativeXAudio2Mixer::PlayPcm16(Bus bus,std::span<const int16_t> samples,
    uint32_t sample_rate,uint16_t channels,float gain) {
  const auto bus_id=BusIndex(bus);
  if(!Ready() || bus_id>=3u || !ValidGain(gain) ||
     !ValidPcm16(samples,sample_rate,channels))return false;
  Update();
  // Bound simultaneous live voices and copy only the source PCM retained by
  // its XAudio2 voice. Caller may release decoded FMOD bank memory at once.
  if(state_->voices.size()>=64u)return false;
  auto voice=std::make_unique<State::Voice>();
  voice->bus=bus_id;
  voice->gain=gain;
  voice->samples.assign(samples.begin(),samples.end());
  WAVEFORMATEX wave{};
  wave.wFormatTag=WAVE_FORMAT_PCM;
  wave.nChannels=channels;
  wave.nSamplesPerSec=sample_rate;
  wave.wBitsPerSample=16;
  wave.nBlockAlign=static_cast<WORD>(channels*sizeof(int16_t));
  wave.nAvgBytesPerSec=wave.nSamplesPerSec*wave.nBlockAlign;
  if(FAILED(state_->engine->CreateSourceVoice(&voice->source,&wave)) ||
     !voice->source)return false;
  XAUDIO2_BUFFER buffer{};
  buffer.Flags=XAUDIO2_END_OF_STREAM;
  buffer.AudioBytes=static_cast<UINT32>(voice->samples.size()*sizeof(int16_t));
  buffer.pAudioData=reinterpret_cast<const BYTE*>(voice->samples.data());
  if(FAILED(voice->source->SubmitSourceBuffer(&buffer)))return false;
  state_->UpdateVolume(*voice);
  if(FAILED(voice->source->Start(0)))return false;
  state_->voices.push_back(std::move(voice));
  return true;
}
bool NativeXAudio2Mixer::PlayFsb4Pcm16(Bus bus,
    std::span<const std::byte> bank,uint32_t sample_index,float gain) {
  if(!Ready())return false;
  const auto decoded=DecodeFsb4Pcm16(bank,sample_index);
  return decoded && PlayPcm16(bus,decoded->pcm,
      decoded->sample_rate,decoded->channels,gain);
}
bool NativeXAudio2Mixer::PlayIndexedWaveBankPcm16(Bus bus,
    const hulk::eor::Index& index,
    const std::filesystem::path& wavebanks_arc,
    uint32_t resource_hash,uint32_t sample_index,float gain) {
  if(!Ready() || index.byte_order()!=hulk::eor::ByteOrder::Big ||
     !resource_hash)return false;
  // The exact 2008 EoR WaveBanks index authenticates the guest resource
  // bytes. Data must still be checked as supported FSB4 PCM before a
  // host audio voice is allocated; XMA/other codecs are never guessed.
  const auto record=index.FindHash("WaveBanks",resource_hash);
  if(!record || !record->size || record->reserved ||
     record->size>(64u<<20))return false;
  try {
    const auto data=hulk::eor::ReadResource(
        wavebanks_arc,*record,64u<<20);
    return PlayFsb4Pcm16(bus,data,sample_index,gain);
  } catch (...) {
    return false;
  }
}
bool NativeXAudio2Mixer::SetBusVolume(Bus bus,float gain) noexcept {
  const unsigned index=BusIndex(bus);
  if(!Ready() || index>=3u || !ValidGain(gain))return false;
  state_->bus_volumes[index]=gain;
  for(auto& voice:state_->voices)if(voice->bus==index)
    state_->UpdateVolume(*voice);
  return true;
}
void NativeXAudio2Mixer::SetMuted(bool muted) noexcept {
  if(!Ready())return;
  state_->muted=muted;
  for(auto& voice:state_->voices)state_->UpdateVolume(*voice);
}
void NativeXAudio2Mixer::StopAll() noexcept {
  if(Ready())state_->voices.clear();
}
size_t NativeXAudio2Mixer::ActiveVoices() const noexcept {
  return Ready()?state_->voices.size():0u;
}
} // namespace hulk::eor::audio
#endif // _WIN32
