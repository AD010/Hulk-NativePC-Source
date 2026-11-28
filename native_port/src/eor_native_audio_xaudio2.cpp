#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include "eor_native_audio_xaudio2.h"
#include "eor_event_voice_group.h"
#include "eor_fmod_doppler.h"
#include "eor_live_dsp_stream.h"
#include "eor_fev_seek_pcm.h"
#include "eor_fsb4_pcm.h"
#include "eor_fsb3_ima.h"
#include "eor_fmod_project.h"
#include "eor_archive.h"
#ifdef _WIN32
#include <xaudio2.h>
#include <x3daudio.h>
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
// One callback object per streamed voice. The three PCM slots never move;
// the producer refills a slot only after XAudio2 releases it via OnBufferEnd.
class NativeLiveDspVoice final : public IXAudio2VoiceCallback {
 public:
  LiveDspStream stream;
  bool Prime(IXAudio2SourceVoice* source,uint16_t channels) noexcept {
    source_=source;channels_=channels;
    for(auto& slot:slots_){
      if(stream.AtEnd())break;
      if(!Submit(slot))return false;
    }
    return true;
  }
  void Disable() noexcept {disabled_.store(true);}
  bool Finished() const noexcept {return finished_.load();}
  void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) noexcept override {}
  void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() noexcept override {}
  void STDMETHODCALLTYPE OnStreamEnd() noexcept override {}
  void STDMETHODCALLTYPE OnBufferStart(void*) noexcept override {}
  void STDMETHODCALLTYPE OnLoopEnd(void*) noexcept override {}
  void STDMETHODCALLTYPE OnVoiceError(void*,HRESULT) noexcept override {finished_.store(true);}
  void STDMETHODCALLTYPE OnBufferEnd(void* context) noexcept override {
    if(disabled_.load()||finished_.load())return;
    for(auto& slot:slots_)if(context==&slot){
      if(slot.last){finished_.store(true);return;}
      if(!stream.AtEnd()&&!Submit(slot))finished_.store(true);
      return;
    }
    finished_.store(true);
  }
 private:
  struct Slot {std::array<int16_t,2048> pcm{};bool last=false;};
  bool Submit(Slot& slot) noexcept {
    if(disabled_.load()||!source_)return false;
    const size_t count=stream.Fill(std::span<int16_t>(slot.pcm).first(1024u*channels_));
    if(!count)return false;
    slot.last=stream.AtEnd();
    XAUDIO2_BUFFER buffer{};
    buffer.Flags=slot.last?XAUDIO2_END_OF_STREAM:0;
    buffer.AudioBytes=static_cast<UINT32>(count*sizeof(int16_t));
    buffer.pAudioData=reinterpret_cast<const BYTE*>(slot.pcm.data());
    buffer.pContext=&slot;
    return SUCCEEDED(source_->SubmitSourceBuffer(&buffer));
  }
  std::array<Slot,3> slots_{};
  IXAudio2SourceVoice* source_=nullptr;
  uint16_t channels_=0;
  std::atomic<bool> disabled_{false},finished_{false};
};
struct NativeXAudio2Mixer::State {
  struct Voice {
    IXAudio2SourceVoice* source=nullptr;
    VoiceId id=0;
    PendingVoiceClock clock;
    std::vector<int16_t> samples;
    std::unique_ptr<NativeLiveDspVoice> live_dsp;
    unsigned bus=0;
    uint16_t channels=0;
    float gain=1.f,base_ratio=1.f,automation_gain=1.f,automation_ratio=1.f,max_ratio=4.f;
    std::vector<FevVoiceBlock> automation;
    std::optional<SpatialVoiceState> spatial;
    std::optional<float> pan,spatial_level;
    std::array<float,128> flat_matrix{};
    uint32_t output_channels=0,sample_rate=0;
    bool looping=false;
    uint32_t first_frame=0;
    FevPlaybackClock automation_clock;
    bool moving_seek=false;
    size_t applied_block=0;
    ~Voice() noexcept {
      if(live_dsp)live_dsp->Disable();
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
  X3DAUDIO_HANDLE spatial_handle{};
  bool spatial_ready=false;
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
  static std::optional<float> EffectivePitch(const Voice& voice,float base,float automation,
      std::optional<float> level) noexcept {
    return ComposeSpatialPitch(base,automation,voice.spatial?voice.spatial->doppler_factor:1.f,
        voice.spatial?(voice.spatial->doppler_from_source?1.f:level.value_or(1.f)):0.f,voice.max_ratio);
  }
  bool UpdateMatrix(Voice& voice,std::optional<float> pan,std::optional<float> level,
      std::optional<float> automation_override={}) noexcept {
    if(!voice.source||!voice.output_channels||voice.output_channels>64u)return false;
    auto flat=voice.flat_matrix;
    const auto count=size_t(voice.channels)*voice.output_channels;
    if(pan){
      const auto stereo=FevStereoMatrix(*pan,voice.channels);
      if(!stereo||voice.output_channels!=2u)return false;
      std::copy_n(stereo->begin(),count,flat.begin());
    }
    auto matrix=flat;
    if(voice.spatial){
      if(!spatial_ready||!ValidSpatialState(*voice.spatial))return false;
      const auto& s=*voice.spatial;
      const auto vector=[](SpatialVector v){return X3DAUDIO_VECTOR{v.x,v.y,v.z};};
      X3DAUDIO_LISTENER listener{};
      listener.Position=vector(s.listener.position);listener.OrientFront=vector(s.listener.front);
      listener.OrientTop=vector(s.listener.up);
      X3DAUDIO_EMITTER emitter{};
      emitter.Position=vector(s.emitter.position);emitter.OrientFront=vector(s.emitter.front);
      emitter.OrientTop=vector(s.emitter.up);emitter.ChannelCount=voice.channels;
      emitter.CurveDistanceScaler=s.distance_scale;emitter.ChannelRadius=s.channel_radius;
      float azimuths[2]={1.5f*X3DAUDIO_PI,.5f*X3DAUDIO_PI};
      if(voice.channels==2u)emitter.pChannelAzimuths=azimuths;
      // The FMOD shadow already calculated its distance gain. Use a flat
      // X3DAudio curve for direction, then apply that gain only to the 3D leg.
      X3DAUDIO_DISTANCE_CURVE_POINT unity_points[2]={{0.f,1.f},{1.f,1.f}};
      X3DAUDIO_DISTANCE_CURVE unity_curve{unity_points,2};
      if(s.distance_gain>=0.f)emitter.pVolumeCurve=&unity_curve;
      std::array<float,128> spatial{};
      X3DAUDIO_DSP_SETTINGS settings{};
      settings.SrcChannelCount=voice.channels;settings.DstChannelCount=voice.output_channels;
      settings.pMatrixCoefficients=spatial.data();
      X3DAudioCalculate(spatial_handle,&listener,&emitter,X3DAUDIO_CALCULATE_MATRIX,&settings);
      if(s.distance_gain>=0.f)for(size_t i=0;i<count;++i)spatial[i]*=s.distance_gain;
      if(!BlendSpatialMatrix(std::span<const float>(flat).first(count),
          std::span<const float>(spatial).first(count),level.value_or(1.f),
          std::span<float>(matrix).first(count)))return false;
    }else if(level)return false;
    const auto ratio=EffectivePitch(voice,voice.base_ratio,automation_override.value_or(voice.automation_ratio),level);
    if(!ratio)return false;
    float previous_ratio=1.f;voice.source->GetFrequencyRatio(&previous_ratio);
    if(FAILED(voice.source->SetFrequencyRatio(*ratio)))return false;
    if(FAILED(voice.source->SetOutputMatrix(master,voice.channels,voice.output_channels,matrix.data(),XAUDIO2_COMMIT_NOW))){
      (void)voice.source->SetFrequencyRatio(previous_ratio);return false;
    }
    voice.pan=pan;voice.spatial_level=level;return true;
  }
  bool UpdatePan(Voice& voice,float pan) noexcept {
    return UpdateMatrix(voice,pan,voice.spatial_level);
  }
  bool ApplyFevBlock(Voice& voice,size_t index) noexcept {
    const auto& controls=voice.automation[index].controls;
    if(controls.pan||controls.pan3d_level||voice.spatial){
      if(!UpdateMatrix(voice,controls.pan?controls.pan:voice.pan,controls.pan3d_level,controls.ratio))return false;
    }else{
      const auto ratio=EffectivePitch(voice,voice.base_ratio,controls.ratio,voice.spatial_level);
      if(!ratio||FAILED(voice.source->SetFrequencyRatio(*ratio)))return false;
    }
    voice.automation_gain=controls.gain;
    voice.automation_ratio=controls.ratio;
    if(!UpdateVolume(voice))return false;
    voice.applied_block=index;
    return true;
  }
  bool UpdateVolume(Voice& voice) noexcept {
    return voice.source && SUCCEEDED(voice.source->SetVolume(muted?0.f:
          voice.gain*voice.automation_gain*bus_volumes[voice.bus],XAUDIO2_COMMIT_NOW));
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
  DWORD channel_mask=0;
  if(SUCCEEDED(next->master->GetChannelMask(&channel_mask))&&channel_mask)
    next->spatial_ready=SUCCEEDED(X3DAudioInitialize(channel_mask,X3DAUDIO_SPEED_OF_SOUND,next->spatial_handle));
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
    if(voice->live_dsp)return voice->live_dsp->Finished();
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
     !ValidVoiceOptions(options,static_cast<uint32_t>(samples.size()/channels)) ||
     !ValidStaticDsp(options.static_dsp,sample_rate))return false;
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
    voice->channels=channels;
    voice->sample_rate=sample_rate;
    voice->looping=options.loop_count!=0;
    voice->spatial=options.spatial;
    voice->gain=gain;
    voice->base_ratio=options.frequency_ratio;
    voice->first_frame=options.start_frame;
    voice->moving_seek=HasMovingFevOffset(options.fev_voice_blocks);
    voice->automation_clock.origin=voice->moving_seek?0u:options.start_frame;
    if(!options.fev_voice_blocks.empty())
      voice->automation.assign(options.fev_voice_blocks.begin(),options.fev_voice_blocks.end());
    voice->clock={options.delay_ms,options.delay_ms!=0,options.start_paused};
    voice->samples.assign(samples.begin(),samples.end());
    if(options.static_dsp.count && !ApplyStaticDsp(voice->samples,sample_rate,channels,options.static_dsp))return false;
    if(options.live_fev_dsp){
      voice->live_dsp=std::make_unique<NativeLiveDspVoice>();
      if(!voice->live_dsp->stream.Initialize(voice->samples,sample_rate,channels,options.start_frame,
          options.dsp_blocks.front().chain))return false;
    }else if(!options.dsp_blocks.empty() &&
       !ApplyScheduledDsp(voice->samples,sample_rate,channels,options.dsp_blocks))return false;
  } catch (...) {return false;}
  WAVEFORMATEX wave{};
  wave.wFormatTag=WAVE_FORMAT_PCM;
  wave.nChannels=channels;
  wave.nSamplesPerSec=sample_rate;
  wave.wBitsPerSample=16;
  wave.nBlockAlign=static_cast<WORD>(channels*sizeof(int16_t));
  wave.nAvgBytesPerSec=wave.nSamplesPerSec*wave.nBlockAlign;
  const auto required=RequiredFevVoiceRatio(options.fev_voice_blocks,options.frequency_ratio);
  if(!required)return false;
  voice->max_ratio=(options.reserve_live_pitch||options.spatial)?kMaxFevVoiceRatio:*required;
  if(FAILED(state_->engine->CreateSourceVoice(&voice->source,&wave,XAUDIO2_VOICE_USEFILTER,voice->max_ratio,voice->live_dsp.get())) ||
     !voice->source)return false;
  XAUDIO2_VOICE_DETAILS details{};state_->master->GetVoiceDetails(&details);
  voice->output_channels=details.InputChannels;
  if(!voice->output_channels||voice->output_channels>64u)return false;
  voice->source->GetOutputMatrix(state_->master,channels,voice->output_channels,voice->flat_matrix.data());
  XAUDIO2_BUFFER buffer{};
  buffer.Flags=XAUDIO2_END_OF_STREAM;
  buffer.AudioBytes=static_cast<UINT32>(voice->samples.size()*sizeof(int16_t));
  buffer.pAudioData=reinterpret_cast<const BYTE*>(voice->samples.data());
  buffer.PlayBegin=options.start_frame;
  buffer.LoopBegin=options.loop_begin;buffer.LoopLength=options.loop_length;
  buffer.LoopCount=options.loop_count;
  const auto filter=NativeFilter(options.filter,options.filter_frequency,options.filter_one_over_q);
  if(FAILED(voice->source->SetFilterParameters(&filter)) ||
     FAILED(voice->source->SetFrequencyRatio(options.frequency_ratio)))return false;
  if(voice->live_dsp){if(!voice->live_dsp->Prime(voice->source,channels))return false;}
  else if(FAILED(voice->source->SubmitSourceBuffer(&buffer)))return false;
  if(!state_->UpdateVolume(*voice))return false;
  if((options.stereo_pan||options.spatial)&&!state_->UpdateMatrix(*voice,options.stereo_pan,options.spatial_level))return false;
  if(!voice->automation.empty()&&!state_->ApplyFevBlock(*voice,FevVoiceBlockAt(voice->automation,voice->automation_clock.Frame())))return false;
  if(!voice->clock.pending && !voice->clock.paused && FAILED(voice->source->Start(0)))return false;
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
bool NativeXAudio2Mixer::PlayEventVariantWithFevDsp(Bus bus,const hulk::eor::Index& index,
    const std::filesystem::path& project_archive,const std::filesystem::path& wave_archive,
    uint32_t project_hash,std::string_view group,std::string_view event_name,
    uint32_t layer,uint32_t instance,uint32_t variant,
    std::span<const FevDspBinding> bindings,std::span<const FevParameterSnapshot> snapshots,
    float gain,VoiceId* started,VoiceOptions options) {
  if(started)*started=0;
  if(!Ready()||!ValidGain(gain)||options.loop_count||options.static_dsp.count||
     !options.dsp_blocks.empty()||!options.fev_voice_blocks.empty())return false;
  try {
    const auto offset=fmod::ResolveEventWaveform(index,project_archive,project_hash,
        group,event_name,layer,instance,variant);
    if(!offset)return false;
    const auto sample=fmod::ReadProjectWaveform(index,project_archive,wave_archive,project_hash,*offset);
    if(!sample)return false;
    const auto blocks=fmod::ResolveEventNamedDsp(index,project_archive,project_hash,
        group,event_name,layer,bindings,snapshots,sample->rate);
    if(!blocks)return false;
    std::optional<std::vector<FevVoiceBlock>> schedule;
    if(bindings.empty()){
      schedule=fmod::ResolveEventVoiceSchedule(index,project_archive,project_hash,group,event_name,layer,snapshots,options.spatial.has_value(),true);
      if(!schedule||schedule->empty())return false;
      const auto& controls=schedule->front().controls;
      if(controls.offset_ms)options.start_frame=FevOffsetFrame(*controls.offset_ms,sample->frames,sample->rate);
      options.fev_voice_blocks=*schedule;
    }
    const Fsb3Sample source{sample->name,sample->frames,sample->mode,sample->rate,
        sample->channels,sample->payload};
    const auto pcm=DecodeOriginalFsb3Sample(source);
    options.dsp_blocks=*blocks;
    if(!pcm)return false;
    if(options.live_fev_dsp&&(!schedule||schedule->size()!=1||blocks->size()!=1))return false;
    // The predeclared offset trajectory and DSP must share event time.
    // Bake only source selection, then run DSP on the continuous playback
    // sequence, rather than jumping through previously filtered PCM.
    if(schedule&&HasMovingFevOffset(*schedule)&&
       std::any_of(blocks->begin(),blocks->end(),[](const auto& b){return b.chain.count!=0;})){
      const auto rendered=RenderFevSeekPcm(*pcm,source.rate,source.channels,*schedule,kMaxNativePcmSamples);
      if(!rendered)return false;
      const size_t frames=rendered->size()/source.channels;
      const auto voice_end=std::lower_bound(schedule->begin(),schedule->end(),frames,
          [](const FevVoiceBlock& b,size_t f){return b.first_frame<f;});
      schedule->erase(voice_end,schedule->end());
      for(auto& b:*schedule)b.controls.offset_ms.reset();
      const auto dsp_end=std::lower_bound(blocks->begin(),blocks->end(),frames,
          [](const DspParameterBlock& b,size_t f){return b.first_frame<f;});
      options.start_frame=0;
      options.fev_voice_blocks=*schedule;
      options.dsp_blocks=std::span<const DspParameterBlock>(*blocks).first(
          static_cast<size_t>(dsp_end-blocks->begin()));
      return PlayPcm16(bus,*rendered,source.rate,source.channels,gain,started,options);
    }
    return PlayPcm16(bus,*pcm,source.rate,source.channels,gain,started,options);
  } catch (...) {return false;}
}
std::optional<std::vector<VoiceId>> NativeXAudio2Mixer::PlayEventGroup(Bus bus,
    const hulk::eor::Index& index,const std::filesystem::path& project_archive,
    const std::filesystem::path& wave_archive,uint32_t project_hash,
    std::string_view group,std::string_view event_name,std::span<const EventCueRequest> cues) {
  if(!Ready())return {};
  std::lock_guard lock(state_->mutex);
  return AdmitEventVoiceGroup<EventCueRequest>(cues,[&](const EventCueRequest& cue) {
    auto options=cue.options;options.start_paused=true;
    VoiceId id=0;
    if(!PlayEventVariant(bus,index,project_archive,wave_archive,project_hash,
        group,event_name,cue.layer,cue.instance,cue.variant,cue.gain,&id,options))
      return VoiceId{0};
    return id;
  },[&](const EventCueRequest& cue,VoiceId id) {
    return cue.options.start_paused || PauseVoice(id,false);
  },[&](VoiceId id) noexcept {(void)StopVoice(id);});
}
size_t NativeXAudio2Mixer::StopEventVoices(std::span<const VoiceId> voices) noexcept {
  if(!Ready())return 0;
  std::lock_guard lock(state_->mutex);
  size_t done=0;
  for(auto id:voices)if(StopVoice(id))++done;
  return done;
}
size_t NativeXAudio2Mixer::PauseEventVoices(std::span<const VoiceId> voices,bool pause) noexcept {
  if(!Ready())return 0;
  std::lock_guard lock(state_->mutex);
  size_t done=0;
  for(auto id:voices)if(PauseVoice(id,pause))++done;
  return done;
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
    // Use the source cursor, not video FPS or wall time. A discontinuity
    // is not a valid original seek; fail rather than replay stale controls.
    if(!v.live_dsp&&!v.clock.pending&&!v.clock.paused&&!v.automation.empty()){
      XAUDIO2_VOICE_STATE status{};v.source->GetState(&status,0);
      if(!status.BuffersQueued){it=state_->voices.erase(it);continue;}
      if(!v.automation_clock.Observe(status.SamplesPlayed)){it=state_->voices.erase(it);ok=false;continue;}
      const auto block=FevVoiceBlockAt(v.automation,v.automation_clock.Frame());
      if(block!=v.applied_block){
        bool applied=false;
        const auto& target=v.automation[block].controls;
        if(v.moving_seek&&target.offset_ms!=v.automation[v.applied_block].controls.offset_ms&&target.offset_ms){
          const auto frame=FevOffsetFrame(*target.offset_ms,
              static_cast<uint32_t>(v.samples.size()/v.channels),v.sample_rate);
          // Native explicit absolute-offset policy; original FMOD elapsed-time
          // correction requires the verified guest event owner's clock.
          applied=ReplaceVoiceAt(v.id,frame,block);
        }else applied=state_->ApplyFevBlock(v,block);
        if(!applied){it=state_->voices.erase(it);ok=false;continue;}
      }
    }
    ++it;
  }
  return ok;
}
bool NativeXAudio2Mixer::SetVoiceFrequencyRatio(VoiceId id,float ratio) noexcept {
  if(!Ready()||!id||!ValidVoiceRatio(ratio))return false;
  std::lock_guard lock(state_->mutex);
  Update();
  for(auto& voice:state_->voices)if(voice->id==id){
    // XAudio2 clamps to the creation-time maximum. Validate the full
    // remaining schedule before changing the base, rather than silently
    // clipping future pitch values or terminating the voice mid-event.
    const auto required=RequiredFevVoiceRatio(voice->automation,ratio);
    if(!required||*required>voice->max_ratio)return false;
    const auto effective=State::EffectivePitch(*voice,ratio,voice->automation_ratio,voice->spatial_level);
    if(!effective||FAILED(voice->source->SetFrequencyRatio(*effective)))return false;
    voice->base_ratio=ratio;return true;
  }
  return false;
}
bool NativeXAudio2Mixer::SetVoiceLiveFevGainPitch(VoiceId id,float gain,float ratio) noexcept {
  if(!Ready()||!id||!ValidGain(gain)||!ValidVoiceRatio(ratio))return false;
  std::lock_guard lock(state_->mutex);
  Update();
  for(auto& voice:state_->voices)if(voice->id==id){
    if(voice->automation.size()!=1||voice->moving_seek)return false;
    const auto effective=State::EffectivePitch(*voice,voice->base_ratio,ratio,voice->spatial_level);
    if(!effective)return false;
    float previous=1.f;voice->source->GetFrequencyRatio(&previous);
    if(FAILED(voice->source->SetFrequencyRatio(*effective)))return false;
    const auto old_gain=voice->automation_gain;
    voice->automation_gain=gain;
    if(!state_->UpdateVolume(*voice)){
      voice->automation_gain=old_gain;(void)voice->source->SetFrequencyRatio(previous);return false;
    }
    voice->automation_ratio=ratio;
    voice->automation[0].controls.gain=gain;voice->automation[0].controls.ratio=ratio;
    return true;
  }
  return false;
}
bool NativeXAudio2Mixer::SetVoiceLiveFevDsp(VoiceId id,const FevEventEntry& event,uint32_t layer,
    std::span<const float> values,bool spatial_ready) noexcept {
  if(!Ready()||!id)return false;
  std::lock_guard lock(state_->mutex);
  Update();
  for(auto& voice:state_->voices)if(voice->id==id){
    if(!voice->live_dsp||voice->automation.size()!=1)return false;
    try {
      const auto controls=EvaluateFevLayerGainPitch(event,layer,values,spatial_ready);
      if(!controls||controls->unhandled||
          controls->values.offset_ms!=voice->automation[0].controls.offset_ms)return false;
      const std::array<FevParameterSnapshot,1> snapshot{{{0,values}}};
      const auto blocks=BuildFevNamedDspBlocks(event,layer,{},snapshot,voice->sample_rate);
      return blocks&&blocks->size()==1&&voice->live_dsp->stream.Publish(blocks->front().chain);
    }catch(...){return false;}
  }
  return false;
}
bool NativeXAudio2Mixer::SetVoicePan(VoiceId id,float pan) noexcept {
  if(!Ready()||!id)return false;
  std::lock_guard lock(state_->mutex);
  Update();
  for(auto& voice:state_->voices)if(voice->id==id)
    return state_->UpdatePan(*voice,pan);
  return false;
}
bool NativeXAudio2Mixer::SetVoiceSpatialState(VoiceId id,const SpatialVoiceState& spatial) noexcept {
  if(!Ready()||!id||!ValidSpatialState(spatial))return false;
  std::lock_guard lock(state_->mutex);
  Update();
  for(auto& voice:state_->voices)if(voice->id==id){
    const auto old=voice->spatial;
    voice->spatial=spatial;
    if(state_->UpdateMatrix(*voice,voice->pan,voice->spatial_level))return true;
    voice->spatial=old;return false;
  }
  return false;
}
bool NativeXAudio2Mixer::SetVoiceSpatialMix(VoiceId id,std::optional<SpatialVoiceState> spatial,
    std::optional<float> pan,std::optional<float> level) noexcept {
  if(!Ready()||!id||(spatial&&!ValidSpatialState(*spatial))||
     (level&&(!spatial||!std::isfinite(*level)||*level<0.f||*level>1.f))||
     (pan&&(!std::isfinite(*pan)||*pan< -1.f||*pan>1.f)))return false;
  std::lock_guard lock(state_->mutex);
  Update();
  for(auto& voice:state_->voices)if(voice->id==id){
    const auto old=voice->spatial;
    voice->spatial=spatial;
    if(state_->UpdateMatrix(*voice,pan,level))return true;
    voice->spatial=old;return false;
  }
  return false;
}
bool NativeXAudio2Mixer::SeekVoiceFrame(VoiceId id,uint32_t frame) noexcept {
  if(!Ready()||!id)return false;
  std::lock_guard lock(state_->mutex);
  Update();
  return ReplaceVoiceAt(id,frame,std::nullopt);
}
bool NativeXAudio2Mixer::ReplaceVoiceAt(VoiceId id,uint32_t frame,std::optional<size_t> automation_block) noexcept {
  for(auto& entry:state_->voices)if(entry->id==id){
    auto& old=*entry;
    // Streaming DSP requires a separately defined history-reset seek policy.
    if(old.live_dsp)return false;
    auto plan=PlanFevVoiceSeek(old.automation,frame,
        static_cast<uint32_t>(old.samples.size()/old.channels),old.looping);
    if(!plan)return false;
    if(automation_block){
      if(!old.moving_seek||*automation_block>=old.automation.size())return false;
      plan->block=*automation_block;
    }else if(old.moving_seek)return false; // independent event clock owns automatic seeks
    // Prepare a stopped replacement. Its buffer borrows the existing
    // voice's owned PCM, which remains resident through the entire swap.
    State::Voice next;
    next.channels=old.channels;next.output_channels=old.output_channels;
    next.sample_rate=old.sample_rate;next.bus=old.bus;next.gain=old.gain;
    next.base_ratio=old.base_ratio;next.max_ratio=old.max_ratio;
    next.automation_gain=old.automation_gain;next.automation_ratio=old.automation_ratio;
    next.flat_matrix=old.flat_matrix;next.spatial=old.spatial;
    next.pan=old.pan;next.spatial_level=old.spatial_level;
    if(!old.automation.empty()){
      const auto& controls=old.automation[plan->block].controls;
      next.automation_gain=controls.gain;next.automation_ratio=controls.ratio;
      if(controls.pan)next.pan=controls.pan;
      next.spatial_level=controls.pan3d_level;
    }
    WAVEFORMATEX wave{};
    wave.wFormatTag=WAVE_FORMAT_PCM;wave.nChannels=old.channels;
    wave.nSamplesPerSec=old.sample_rate;wave.wBitsPerSample=16;
    wave.nBlockAlign=static_cast<WORD>(old.channels*sizeof(int16_t));
    wave.nAvgBytesPerSec=wave.nSamplesPerSec*wave.nBlockAlign;
    if(FAILED(state_->engine->CreateSourceVoice(&next.source,&wave,
        XAUDIO2_VOICE_USEFILTER,next.max_ratio))||!next.source)return false;
    XAUDIO2_FILTER_PARAMETERS filter{};old.source->GetFilterParameters(&filter);
    if(FAILED(next.source->SetFilterParameters(&filter))||
       !state_->UpdateVolume(next)||
       !state_->UpdateMatrix(next,next.pan,next.spatial_level))return false;
    XAUDIO2_BUFFER buffer{};
    buffer.Flags=XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes=static_cast<UINT32>(old.samples.size()*sizeof(int16_t));
    buffer.pAudioData=reinterpret_cast<const BYTE*>(old.samples.data());
    buffer.PlayBegin=plan->frame;
    if(FAILED(next.source->SubmitSourceBuffer(&buffer)))return false;
    // Do not destroy the original until the replacement is admitted.
    const bool running=!old.clock.paused&&!old.clock.pending;
    if(running){
      if(FAILED(old.source->Stop(0)))return false;
      if(FAILED(next.source->Start(0))){
        (void)old.source->Start(0);
        return false;
      }
    }
    std::swap(old.source,next.source); // next's destructor retires old source
    old.first_frame=plan->frame;
    if(automation_block)old.automation_clock.Rebase();
    else old.automation_clock={plan->frame,0};
    old.applied_block=plan->block;
    old.automation_gain=next.automation_gain;old.automation_ratio=next.automation_ratio;
    old.pan=next.pan;old.spatial_level=next.spatial_level;
    return true;
  }
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
