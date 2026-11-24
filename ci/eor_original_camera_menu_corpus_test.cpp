#include "../native_port/src/eor_original_pause_vm.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
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
  constexpr uint32_t vm=0x1000,desc=0x4000,ctx=0x4100,page=0x4200,res=0x4300,def=0x4400,name=0x4500;
  constexpr uint32_t frame=0x5000,input=0x5100,nested=0x5200,arg=0x5300;
  uint8_t pitch=0,yaw=0;
  auto put=[&](uint32_t a,uint32_t x){for(unsigned i=0;i<4;++i)heap.at(a+i)=uint8_t(x>>(24-8*i));};
  auto read=[&](uint32_t a,uint8_t* dst,size_t n){
    if(n==1 && a==0x82e53b3cu){*dst=pitch;return true;}
    if(n==1 && a==0x82e53b3du){*dst=yaw;return true;}
    if(a>=0x82000000u && uint64_t(a)-0x82000000u+n<=image.size()){
      std::copy_n(image.data()+a-0x82000000u,n,dst);return true;
    }
    if(uint64_t(a)+n<=heap.size()){std::copy_n(heap.data()+a,n,dst);return true;}
    return false;
  };
  put(vm,frame);put(frame,input);put(input+8,nested);put(nested,arg);
  put(ctx+20,page);put(vm+32,3);put(vm+4*(2093+3),page);
  put(page+124,res);put(res+8,def);put(def,0x8206a4d0);put(def+4,name|1);
  for(bool game:{false,true}){
    put(def+12,game?0x04aa3a30u:0xd7a9e027u);
    const std::string_view label=game?"UIPage_Pause_GameSettings":"UIPage_Pause_Settings";
    std::fill_n(heap.data()+name,32,0);std::copy(label.begin(),label.end(),heap.data()+name);
    for(bool axis:{false,true}){
      const uint32_t method=axis?0x82339a08u:0x823399e8u;
      put(desc,axis?0x821204b0u:0x8217e250u);
      for(bool v:{false,true}){
        auto& previous=axis?yaw:pitch;previous=!v;heap[arg+12]=v;
        const auto change=CaptureCameraMenuChange(method,0x825ea158,vm,vm,desc,ctx,read);
        check(change && change->yaw==axis && change->value==v);
        previous=v;check(!CaptureCameraMenuChange(method,0x825ea158,vm,vm,desc,ctx,read));
        previous=!v;check(!CaptureCameraMenuChange(method,0,vm,vm,desc,ctx,read));
        heap[arg+12]=2;check(!CaptureCameraMenuChange(method,0x825ea158,vm,vm,desc,ctx,read));
      }
    }
  }
  put(def+12,0);yaw=0;heap[arg+12]=1;
  check(!CaptureCameraMenuChange(0x82339a08,0x825ea158,vm,vm,desc,ctx,read));
  std::cout<<"PASS: both original pages, both axes, true/false and rejected events\n";
}
