#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include "eor_native_audio_xaudio2.h"
#include "eor_fsb4_pcm.h"
#include "eor_fsb3_ima.h"
#include "eor_fmod_project.h"
#include "eor_archive.h"
#ifdef _WIN32
#include <xaudio2.h>
#include <objbase.h>
#include "eor_fsb3_mpeg_win.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <utility>
#include <vector>

namespace hulk::eor::audio {
namespace {
unsigned BusIndex(Bus bus) noexcept {return static_cast<unsigned>(bus);}
XAUDIO2_FILTER_PARAMETERS NativeFilter(VoiceFilter filter,float frequency,float one_over_q) noexcept {
  if(filter==VoiceFilter::Bypass)return {LowPassFilter,1.f,1.f};
  return {filter==VoiceFilter::HighPass?HighPassFilter:LowPassFilter,frequency,one_over_q};
}
bool ValidGain(float gain) noexcept {
  return std::isfinite(gain) && gain>=0.f && gain<=1.f;
}
}
struct NativeXAudio2Mixer::State {
  struct Voice {
    IXAudio2SourceVoice* source=nullptr;
    VoiceId id=0;
    PendingVoiceClock clock;
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
  VoiceId last_voice_id=0;
  // FMOD event callbacks can originate from different host worker threads.
  // Protect live voice and bus state; recursive lock permits PlayPcm16()
  // to prune completed voices through Update() without a second unsafe path.
  mutable std::recursive_mutex mutex;
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
  bool UpdateVolume(Voice& voice) noexcept {
    return voice.source && SUCCEEDED(voice.source->SetVolume(muted?0.f:
          voice.gain*bus_volumes[voice.bus],XAUDIO2_COMMIT_NOW));
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
  std::lock_guard lock(state_->mutex);
  auto& v=state_->voices;
  v.erase(std::remove_if(v.begin(),v.end(),[](const auto& voice) {
    if(!voice || !voice->source)return true;
    XAUDIO2_VOICE_STATE status{};
    voice->source->GetState(&status,XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return status.BuffersQueued==0;
  }),v.end());
}
bool NativeXAudio2Mixer::PlayPcm16(Bus bus,std::span<const int16_t> samples,
    uint32_t sample_rate,uint16_t channels,float gain,VoiceId* started,VoiceOptions options) {
  if(started)*started=0;
  const auto bus_id=BusIndex(bus);
  if(!Ready() || bus_id>=3u || !ValidGain(gain) ||
     !ValidPcm16(samples,sample_rate,channels) ||
     !ValidVoiceOptions(options,static_cast<uint32_t>(samples.size()/channels)))return false;
  std::lock_guard lock(state_->mutex);
  Update();
  // Bound simultaneous live voices and copy only the source PCM retained by
  // its XAudio2 voice. Caller may release decoded FMOD bank memory at once.
  if(state_->voices.size()>=64u || state_->last_voice_id==UINT64_MAX)return false;
  // One bounded original PCM sample can be long-form dialogue or music.
  // Cap TOTAL live voice memory as well; dozens of large simultaneous
  // events must not exhaust RAM or make the game's 3D world stutter.
  constexpr size_t kMaxLiveVoiceSamples=48u<<20; // 96 MiB PCM16
  size_t retained=0;
  for(const auto& active:state_->voices)
    retained+=active->samples.size();
  if(retained>kMaxLiveVoiceSamples ||
     samples.size()>kMaxLiveVoiceSamples-retained)return false;
  std::unique_ptr<State::Voice> voice;
  try {
    // No allocation may fail after playback has started. Keep source
    // sample ownership and the voice registry ready before touching XAudio2.
    if(state_->voices.capacity()<64u)state_->voices.reserve(64u);
    voice=std::make_unique<State::Voice>();
    voice->bus=bus_id;
    voice->gain=gain;
    voice->clock={options.delay_ms,options.delay_ms!=0,false};
    voice->samples.assign(samples.begin(),samples.end());
  } catch (...) {return false;}
  WAVEFORMATEX wave{};
  wave.wFormatTag=WAVE_FORMAT_PCM;
  wave.nChannels=channels;
  wave.nSamplesPerSec=sample_rate;
  wave.wBitsPerSample=16;
  wave.nBlockAlign=static_cast<WORD>(channels*sizeof(int16_t));
  wave.nAvgBytesPerSec=wave.nSamplesPerSec*wave.nBlockAlign;
  if(FAILED(state_->engine->CreateSourceVoice(&voice->source,&wave,XAUDIO2_VOICE_USEFILTER,4.f)) ||
     !voice->source)return false;
  XAUDIO2_BUFFER buffer{};
  buffer.Flags=XAUDIO2_END_OF_STREAM;
  buffer.AudioBytes=static_cast<UINT32>(voice->samples.size()*sizeof(int16_t));
  buffer.pAudioData=reinterpret_cast<const BYTE*>(voice->samples.data());
  buffer.LoopBegin=options.loop_begin;buffer.LoopLength=options.loop_length;
  buffer.LoopCount=options.loop_count;
  const auto filter=NativeFilter(options.filter,options.filter_frequency,options.filter_one_over_q);
  if(FAILED(voice->source->SetFilterParameters(&filter)) ||
     FAILED(voice->source->SetFrequencyRatio(options.frequency_ratio)) ||
     FAILED(voice->source->SubmitSourceBuffer(&buffer)))return false;
  if(!state_->UpdateVolume(*voice))return false;
  if(!voice->clock.pending && FAILED(voice->source->Start(0)))return false;
  // Publish an ID only after a real XAudio2 buffer has been accepted.
  voice->id=++state_->last_voice_id;
  const auto id=voice->id;
  state_->voices.push_back(std::move(voice));
  if(started)*started=id;
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
  if(!Ready() || !resource_hash)return false;
  // Accept a real Xbox USA (big-endian index) OR official PC 2008
  // (little-endian index) WaveBanks archive. Shared resource hashes are
  // names, not permission to mix indices and different platform ARC
  // payloads. The original ARC's little-endian byte-count header must
  // agree exactly with the selected index's highest WaveBanks ending.
  // This provides the native PC audio-data branch without touching the
  // running Xbox world, XEX shader set, or 32-bit FMOD binaries.
  const auto group=index.groups().find("WaveBanks");
  if(group==index.groups().end() || group->second.empty())return false;
  const auto record=index.FindHash("WaveBanks",resource_hash);
  if(!record || !record->size || record->reserved ||
     record->size>(64u<<20))return false;
  uint64_t end=0;
  for(const auto& entry:group->second) {
    if(entry.reserved || entry.offset<4u || !entry.size)return false;
    // Windows SDK headers may define a function-like max macro.
    end=(std::max)(end,uint64_t(entry.offset)+entry.size);
  }
  if(end<4 || end>UINT32_MAX)return false;
  try {
    const auto file_size=std::filesystem::file_size(wavebanks_arc);
    if(file_size<end)return false;
    std::ifstream archive(wavebanks_arc,std::ios::binary);
    std::array<uint8_t,4> header{};
    if(!archive.read(reinterpret_cast<char*>(header.data()),header.size()))
      return false;
    const auto declared=uint32_t(header[0])|
        uint32_t(header[1])<<8u|uint32_t(header[2])<<16u|
        uint32_t(header[3])<<24u;
    if(declared!=end)return false;
    const auto data=hulk::eor::ReadResource(
        wavebanks_arc,*record,64u<<20);
    if(const auto bank=InspectOriginalWaveBank(data,resource_hash,index.byte_order()==ByteOrder::Big)) {
      if(sample_index>=bank->samples.size())return false;
      const auto& sample=bank->samples[sample_index];
      const auto pcm=DecodeOriginalFsb3Sample(sample);
      return pcm && PlayPcm16(bus,*pcm,sample.rate,sample.channels,gain);
    }
    return PlayFsb4Pcm16(bus,data,sample_index,gain);
  } catch (...) {
    return false;
  }
}
bool NativeXAudio2Mixer::PlayProjectWaveSample(Bus bus,const hulk::eor::Index& index,
    const std::filesystem::path& project_archive,const std::filesystem::path& wave_archive,
    uint32_t project_hash,uint32_t bank_ordinal,uint32_t sample_ordinal,float gain,
    VoiceId* started) {
  if(started)*started=0;
  if(!Ready()||!ValidGain(gain))return false;
  try {
    const auto sample=fmod::ReadProjectWaveSample(index,project_archive,wave_archive,
        project_hash,bank_ordinal,sample_ordinal);
    if(!sample)return false;
    const Fsb3Sample source{sample->name,sample->frames,sample->mode,sample->rate,
        sample->channels,sample->payload};
    const auto pcm=DecodeOriginalFsb3Sample(source);
    return pcm && PlayPcm16(bus,*pcm,source.rate,source.channels,gain,started);
  } catch (...) {return false;}
}
bool NativeXAudio2Mixer::PlayProjectWaveform(Bus bus,const hulk::eor::Index& index,
    const std::filesystem::path& project_archive,const std::filesystem::path& wave_archive,
    uint32_t project_hash,size_t fev_offset,float gain,VoiceId* started,VoiceOptions options) {
  if(started)*started=0;
  if(!Ready()||!ValidGain(gain))return false;
  try {
    const auto sample=fmod::ReadProjectWaveform(index,project_archive,wave_archive,project_hash,fev_offset);
    if(!sample)return false;
    const Fsb3Sample source{sample->name,sample->frames,sample->mode,sample->rate,
        sample->channels,sample->payload};
    const auto pcm=DecodeOriginalFsb3Sample(source);
    return pcm && PlayPcm16(bus,*pcm,source.rate,source.channels,gain,started,options);
  } catch (...) {return false;}
}
bool NativeXAudio2Mixer::PlayEventVariant(Bus bus,const hulk::eor::Index& index,
    const std::filesystem::path& project_archive,const std::filesystem::path& wave_archive,
    uint32_t project_hash,std::string_view group,std::string_view event_name,
    uint32_t layer,uint32_t instance,uint32_t variant,float gain,VoiceId* started,VoiceOptions options) {
  if(started)*started=0;
  if(!Ready() || !ValidGain(gain))return false;
  try {
    const auto offset=fmod::ResolveEventWaveform(index,project_archive,project_hash,
        group,event_name,layer,instance,variant);
    return offset && PlayProjectWaveform(bus,index,project_archive,wave_archive,
        project_hash,*offset,gain,started,options);
  } catch (...) {return false;}
}
bool NativeXAudio2Mixer::StopVoice(VoiceId id) noexcept {
  if(!Ready()||!id)return false;
  std::lock_guard lock(state_->mutex);
  const auto it=std::find_if(state_->voices.begin(),state_->voices.end(),
      [id](const auto& voice){return voice->id==id;});
  if(it==state_->voices.end())return false;
  state_->voices.erase(it);
  return true;
}
bool NativeXAudio2Mixer::PauseVoice(VoiceId id,bool pause) noexcept {
  if(!Ready()||!id)return false;
  std::lock_guard lock(state_->mutex);
  Update();
  for(auto& voice:state_->voices)if(voice->id==id) {
    if(voice->clock.paused==pause)return true;
    if(!voice->clock.pending) {
      const auto hr=pause?voice->source->Stop(0):voice->source->Start(0);
      if(FAILED(hr))return false;
    }
    voice->clock.paused=pause;
    return true;
  }
  return false;
}
bool NativeXAudio2Mixer::AdvanceScheduledVoices(uint32_t elapsed_ms) noexcept {
  if(!Ready())return false;
  std::lock_guard lock(state_->mutex);
  Update();
  bool ok=true;
  for(auto it=state_->voices.begin();it!=state_->voices.end();) {
    auto& v=**it;
    if(v.clock.Advance(elapsed_ms)) {
      if(FAILED(v.source->Start(0))) {it=state_->voices.erase(it);ok=false;continue;}
      v.clock.pending=false;
    }
    ++it;
  }
  return ok;
}
bool NativeXAudio2Mixer::SetVoiceFrequencyRatio(VoiceId id,float ratio) noexcept {
  if(!Ready()||!id||!ValidVoiceRatio(ratio))return false;
  std::lock_guard lock(state_->mutex);
  Update();
  for(auto& voice:state_->voices)if(voice->id==id)
    return SUCCEEDED(voice->source->SetFrequencyRatio(ratio));
  return false;
}
bool NativeXAudio2Mixer::SetVoiceFilter(VoiceId id,VoiceFilter filter,float frequency,float one_over_q) noexcept {
  if(!Ready()||!id||!ValidVoiceFilter(filter,frequency,one_over_q))return false;
  const auto parameters=NativeFilter(filter,frequency,one_over_q);
  std::lock_guard lock(state_->mutex);
  Update();
  for(auto& voice:state_->voices)if(voice->id==id)
    return SUCCEEDED(voice->source->SetFilterParameters(&parameters));
  return false;
}
bool NativeXAudio2Mixer::SetVoiceGain(VoiceId id,float gain) noexcept {
  if(!Ready()||!id||!ValidGain(gain))return false;
  std::lock_guard lock(state_->mutex);
  Update();
  for(auto& voice:state_->voices)if(voice->id==id) {
    const auto previous=voice->gain;
    voice->gain=gain;
    if(state_->UpdateVolume(*voice))return true;
    voice->gain=previous;
    return false;
  }
  return false;
}
bool NativeXAudio2Mixer::SetBusVolume(Bus bus,float gain) noexcept {
  const unsigned index=BusIndex(bus);
  if(!Ready() || index>=3u || !ValidGain(gain))return false;
  std::lock_guard lock(state_->mutex);
  state_->bus_volumes[index]=gain;
  bool ok=true;
  for(auto& voice:state_->voices)if(voice->bus==index)
    if(!state_->UpdateVolume(*voice))ok=false;
  return ok;
}
void NativeXAudio2Mixer::SetMuted(bool muted) noexcept {
  if(!Ready())return;
  std::lock_guard lock(state_->mutex);
  state_->muted=muted;
  for(auto& voice:state_->voices)state_->UpdateVolume(*voice);
}
void NativeXAudio2Mixer::StopAll() noexcept {
  if(!Ready())return;
  std::lock_guard lock(state_->mutex);
  state_->voices.clear();
}
size_t NativeXAudio2Mixer::ActiveVoices() const noexcept {
  if(!Ready())return 0u;
  std::lock_guard lock(state_->mutex);
  return state_->voices.size();
}
} // namespace hulk::eor::audio
#endif // _WIN32
