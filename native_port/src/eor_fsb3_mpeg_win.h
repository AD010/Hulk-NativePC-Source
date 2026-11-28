// Windows MPEG-1/2 Layer II decoder via the documented synchronous
// Microsoft MPEG audio MFT. No external executable, FMOD DLL or temp file.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "eor_native_audio_xaudio2.h"
#include "eor_fsb3_ima.h"
#include "eor_fsb3_mpeg.h"
#include <algorithm>
#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#include <cstring>
namespace hulk::eor::audio {
namespace {
using Microsoft::WRL::ComPtr;
struct MediaFoundationSession {
  bool com=false,mf=false;
  MediaFoundationSession() noexcept {
    const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    com=SUCCEEDED(hr);
    if(com||hr==RPC_E_CHANGED_MODE)mf=SUCCEEDED(MFStartup(MF_VERSION,MFSTARTUP_LITE));
  }
  ~MediaFoundationSession() {if(mf)MFShutdown();if(com)CoUninitialize();}
};
inline bool SelectMpegPcmOutput(IMFTransform& decoder,const Fsb3Sample& s) {
  for(DWORD i=0;i<32u;++i) {
    ComPtr<IMFMediaType> type;
    if(FAILED(decoder.GetOutputAvailableType(0,i,type.GetAddressOf())))return false;
    GUID subtype{};
    UINT32 channels=0,rate=0,bits=0;
    if(SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE,&subtype))&&subtype==MFAudioFormat_PCM&&
       SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&channels))&&channels==s.channels&&
       SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&rate))&&rate==s.rate&&
       SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,&bits))&&bits==16u&&
       SUCCEEDED(decoder.SetOutputType(0,type.Get(),0)))return true;
  }
  return false;
}
inline std::optional<std::vector<int16_t>> DecodeOriginalMpegMft(const Fsb3Sample& s) {
  const auto frames=OriginalMpegLayer2Frames(s);
  const uint64_t total=uint64_t(s.frames)*s.channels;
  if(!frames||!total||total>kMaxNativePcmSamples)return {};
  MediaFoundationSession session;
  if(!session.mf)return {};
  ComPtr<IMFTransform> decoder;
  if(FAILED(CoCreateInstance(CLSID_MSMPEGAudDecMFT,nullptr,CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(decoder.GetAddressOf()))))return {};
  ComPtr<IMFMediaType> input;
  if(FAILED(MFCreateMediaType(input.GetAddressOf()))||
     FAILED(input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio))||
     FAILED(input->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_MPEG))||
     FAILED(input->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,s.channels))||
     FAILED(input->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,s.rate))||
     FAILED(decoder->SetInputType(0,input.Get(),0))||
     !SelectMpegPcmOutput(*decoder.Get(),s))return {};
  if(FAILED(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0))||
     FAILED(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0)))return {};
  std::vector<int16_t> pcm;
  pcm.reserve(static_cast<size_t>(total));
  unsigned stream_changes=0;
  // Drain every available output sample before feeding more compressed frames.
  // Bounded loop also rejects a broken transform producing empty output forever.
  auto pull=[&]() -> bool {
    for(unsigned loop=0;loop<1024u;++loop) {
      MFT_OUTPUT_STREAM_INFO info{};
      if(FAILED(decoder->GetOutputStreamInfo(0,&info)))return false;
      ComPtr<IMFSample> supplied;
      if(!(info.dwFlags&(MFT_OUTPUT_STREAM_PROVIDES_SAMPLES|MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES))) {
        ComPtr<IMFMediaBuffer> buffer;
        const DWORD capacity=(std::max)(info.cbSize,1152u*DWORD(s.channels)*2u);
        if(capacity>(4u<<20))return false;
        const auto hr=info.cbAlignment?
            MFCreateAlignedMemoryBuffer(capacity,info.cbAlignment-1u,buffer.GetAddressOf()):
            MFCreateMemoryBuffer(capacity,buffer.GetAddressOf());
        if(FAILED(hr)||FAILED(MFCreateSample(supplied.GetAddressOf()))||
           FAILED(supplied->AddBuffer(buffer.Get())))return false;
      }
      MFT_OUTPUT_DATA_BUFFER out{};
      out.dwStreamID=0;out.pSample=supplied.Get();
      DWORD status=0;
      const auto hr=decoder->ProcessOutput(0,1,&out,&status);
      ComPtr<IMFSample> returned;
      if(out.pSample&&out.pSample!=supplied.Get())returned.Attach(out.pSample);
      if(out.pEvents)out.pEvents->Release();
      if(hr==MF_E_TRANSFORM_NEED_MORE_INPUT)return true;
      if(hr==MF_E_TRANSFORM_STREAM_CHANGE) {
        if(++stream_changes>4u||!SelectMpegPcmOutput(*decoder.Get(),s))return false;
        continue;
      }
      if(FAILED(hr)||!out.pSample)return false;
      ComPtr<IMFMediaBuffer> buffer;
      if(FAILED(out.pSample->ConvertToContiguousBuffer(buffer.GetAddressOf())))return false;
      BYTE* ptr=nullptr;DWORD length=0;
      if(FAILED(buffer->Lock(&ptr,nullptr,&length)))return false;
      const bool ok=ptr&&length&&length%(sizeof(int16_t)*s.channels)==0&&
          length/sizeof(int16_t)<=total-pcm.size();
      if(ok) {
        const auto offset=pcm.size();
        pcm.resize(offset+length/sizeof(int16_t)); // pre-reserved exact capacity
        std::memcpy(pcm.data()+offset,ptr,length);
      }
      buffer->Unlock();
      if(!ok)return false;
    }
    return false;
  };
  LONGLONG pos=0;
  for(const auto frame:*frames) {
    ComPtr<IMFMediaBuffer> buffer;ComPtr<IMFSample> sample;
    if(FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(frame.size()),buffer.GetAddressOf())))return {};
    BYTE* ptr=nullptr;
    if(FAILED(buffer->Lock(&ptr,nullptr,nullptr)))return {};
    std::memcpy(ptr,frame.data(),frame.size());buffer->Unlock();
    const auto start=pos*10000000ll/s.rate;
    pos+=1152;
    const auto end=pos*10000000ll/s.rate;
    if(FAILED(buffer->SetCurrentLength(static_cast<DWORD>(frame.size())))||
       FAILED(MFCreateSample(sample.GetAddressOf()))||
       FAILED(sample->AddBuffer(buffer.Get()))||
       FAILED(sample->SetSampleTime(start))||
       FAILED(sample->SetSampleDuration(end-start)))return {};
    auto hr=decoder->ProcessInput(0,sample.Get(),0);
    if(hr==MF_E_NOTACCEPTING) {
      if(!pull())return {};
      hr=decoder->ProcessInput(0,sample.Get(),0);
    }
    if(FAILED(hr)||!pull())return {};
  }
  if(FAILED(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM,0))||
     FAILED(decoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN,0))||!pull())return {};
  decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING,0);
  if(pcm.size()!=total||!ValidPcm16(pcm,s.rate,s.channels))return {};
  return pcm;
}
inline std::optional<std::vector<int16_t>> DecodeOriginalFsb3Sample(const Fsb3Sample& sample) {
  if(sample.mode&0x00400000u)return DecodeOriginalIma(sample);
  if(sample.mode&0x200u)return DecodeOriginalMpegMft(sample);
  return {};
}
} // namespace
} // namespace hulk::eor::audio
