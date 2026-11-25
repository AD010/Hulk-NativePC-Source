#include "../native_port/src/eor_original_audio_menu.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
using namespace hulk::eor::vm;
static void check(bool v){if(!v)std::abort();}
int main(int argc,char** argv){
  if(argc!=2)return 2;
  std::ifstream file(argv[1],std::ios::binary);
  std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)),{});
  if(image.empty())return 2;
  std::array<uint8_t,0x10000> heap{};
  std::array<uint8_t,12> active{},deferred{};
  uint8_t lock=0;
  constexpr uint32_t vm=0x1000,desc=0x4000,ctx=0x4100,page=0x4200,res=0x4300,def=0x4400,name=0x4500;
  constexpr uint32_t frame=0x5000,input=0x5100,nested=0x5200,arg=0x5300;
  auto put=[&](uint32_t a,uint32_t x){for(unsigned i=0;i<4;++i)heap.at(a+i)=uint8_t(x>>(24-8*i));};
  auto volume=[&](auto& buf,unsigned ch,uint32_t v){
    const auto bits=std::bit_cast<uint32_t>(float(v)*0.01f);
    for(unsigned i=0;i<4;++i)buf[ch*4+i]=uint8_t(bits>>(24-8*i));
  };
  auto read=[&](uint32_t a,uint8_t* dst,size_t n){
    if(a==kOriginalVolumeLock && n==1){*dst=lock;return true;}
    for(auto pair:{std::pair{kOriginalActiveVolume,&active},std::pair{kOriginalDeferredVolume,&deferred}})
      if(a>=pair.first && uint64_t(a)-pair.first+n<=12){
        std::copy_n(pair.second->data()+a-pair.first,n,dst);return true;
      }
    if(a>=0x82000000u && uint64_t(a)-0x82000000u+n<=image.size()){
      std::copy_n(image.data()+a-0x82000000u,n,dst);return true;
    }
    if(uint64_t(a)+n<=heap.size()){std::copy_n(heap.data()+a,n,dst);return true;}
    return false;
  };
  put(vm,frame);put(frame,input);put(input+8,nested);put(nested,arg);
  put(ctx+20,page);put(vm+32,3);put(vm+4*(2093+3),page);
  put(page+124,res);put(res+8,def);put(def,0x8206a4d0);put(def+4,name|1);
  put(def+12,0x04aa3a30u);
  const std::string_view label="UIPage_Pause_GameSettings";
  std::copy(label.begin(),label.end(),heap.data()+name);
  for(unsigned ch=0;ch<3;++ch){
    const auto spec=kAudioMenuMethods[ch];put(desc,spec.thunk);
    for(unsigned l=0;l<=1;++l){
      lock=uint8_t(l);
      for(uint32_t v=0;v<=100;++v){
        put(arg+12,v);
        auto change=CaptureAudioMenuChange(spec.method,0x825ea158,vm,vm,desc,ctx,read);
        check(change && change->value==v && change->channel==spec.channel && change->was_locked==bool(l));
        volume(lock?deferred:active,ch,v);
        check(OriginalVolumeApplied(*change,read));
        volume(lock?deferred:active,ch,(v+1)%101);
        check(!OriginalVolumeApplied(*change,read));
      }
      put(arg+12,101);
      check(!CaptureAudioMenuChange(spec.method,0x825ea158,vm,vm,desc,ctx,read));
    }
  }
  const auto spec=kAudioMenuMethods[3];put(desc,spec.thunk);
  lock=0;heap[arg+12]=1;
  const auto change=CaptureAudioMenuChange(spec.method,0x825ea158,vm,vm,desc,ctx,read);
  check(change && change->channel==AudioChannel::Lock);
  for(unsigned i=0;i<3;++i)volume(active,i,50);
  deferred=active;lock=1;
  auto plan=PlanDeferredVolumeRestore(*change,{70,-1,20},read);
  check(plan && plan->before==active);
  auto expected=active;volume(expected,0,70);volume(expected,2,20);
  check(plan->after==expected);
  check(!PlanDeferredVolumeRestore(*change,{101,-1,20},read));
  volume(deferred,1,60);
  check(!PlanDeferredVolumeRestore(*change,{70,-1,20},read));
}
