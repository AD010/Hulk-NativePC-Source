#include "../native_port/src/eor_original_havok_step.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <vector>
using namespace hulk::eor::havok;
static void check(bool v) { if(!v) std::abort(); }
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  std::ifstream file(argv[1],std::ios::binary);
  std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)),{});
  if(image.empty())return 2;
  std::array<uint8_t,4096> heap{};
  constexpr uint32_t w=0x1000,s=0x1100,j=0x1200;
  auto put=[&](uint32_t a,uint32_t x){for(unsigned i=0;i<4;++i)heap.at(a-w+i)=uint8_t(x>>(24-8*i));};
  auto read=[&](uint32_t a,uint8_t* dst,size_t n){
    if(a>=0x82000000u && uint64_t(a)-0x82000000u+n<=image.size()) {
      std::copy_n(image.data()+a-0x82000000u,n,dst);return true;
    }
    if(a>=w && uint64_t(a)-w+n<=heap.size()) {std::copy_n(heap.data()+a-w,n,dst);return true;}
    return false;
  };
  put(w,0x82051ef0);put(w+8,s);put(s+8,w);put(s+16,std::bit_cast<uint32_t>(2.0f));
  put(s+20,std::bit_cast<uint32_t>(2.0f));put(s+24,std::bit_cast<uint32_t>(1.0f/30));put(j,w);
  for(auto vt:{0x82050d94u,0x82053104u,0x8205272cu}) {
    put(s,vt);
    for(unsigned fps=30;fps<=240;++fps) {
      float dt=1.0f/fps;put(j+4,std::bit_cast<uint32_t>(dt));
      auto v=CaptureStep(0x82509c18,0,w,0,dt,read);
      check(v && v->seconds==dt && v->world==w && v->simulation==s && !v->split);
      for(auto lr:{0x8296b898u,0x8296b97cu}) {
        v=CaptureStep(0x82508f60,lr,w,j,dt,read);
        check(bool(v)==(vt==0x8205272cu));
        if(v)check(v->split && v->scheduler==j && v->seconds==dt);
      }
    }
  }
  const float dt=1.0f/60;put(j+4,std::bit_cast<uint32_t>(dt));
  check(!CaptureStep(0x82508f60,0,w,j,dt,read));
  check(!CaptureStep(0x82508f60,0x8296b898,w,j,dt*2,read));
  check(!CaptureStep(0x82509c18,0,w,0,std::numeric_limits<double>::quiet_NaN(),read));
  put(s+8,w+4);check(!CaptureStep(0x82509c18,0,w,0,dt,read));put(s+8,w);
  image.at(0x82508f60u-0x82000000u)^=1;
  check(!CaptureStep(0x82508f60,0x8296b898,w,j,dt,read));
  std::cout<<"PASS: 633 world steps, 1266 split call-site checks and invalid inputs\n";
}
