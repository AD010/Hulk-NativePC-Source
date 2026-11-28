// Generated silence only; no retail assets, no audio endpoint needed.
#include "../native_port/src/eor_fsb3_mpeg_win.h"
#include <iostream>
using namespace hulk::eor::audio;
int main() {
  MediaFoundationSession mf;
  if(!mf.mf){std::cout<<"SKIP: Media Foundation unavailable\n";return 77;}
  {
    Microsoft::WRL::ComPtr<IMFTransform> decoder;
    const auto hr=CoCreateInstance(CLSID_MSMPEGAudDecMFT,nullptr,CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(decoder.GetAddressOf()));
    if(hr==REGDB_E_CLASSNOTREG){std::cout<<"SKIP: MPEG MFT unavailable\n";return 77;}
    if(FAILED(hr))return 1;
  }
  std::vector<std::byte> bytes(4u*384u);
  for(unsigned i=0;i<4;++i) {
    const auto p=384u*i;
    bytes[p]=std::byte{0xff};bytes[p+1]=std::byte{0xfd};
    bytes[p+2]=std::byte{0x84};bytes[p+3]=std::byte{0xc0};
  }
  const Fsb3Sample sample{"generated_silence",4608,0x2220,48000,1,bytes};
  const auto pcm=DecodeOriginalMpegMft(sample);
  if(!pcm||pcm->size()!=4608u)return 2;
  for(auto value:*pcm)if(value<-1||value>1)return 3;
  std::cout<<"Windows MPEG Layer II MFT -> PCM16 PASS\n";
  return 0;
}
