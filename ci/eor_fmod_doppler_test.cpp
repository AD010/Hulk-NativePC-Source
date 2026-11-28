#include "../native_port/src/eor_fmod_doppler.h"
#include <cassert>
#include <limits>
using namespace hulk::eor::audio;
int main(){
  SpatialVector l{},s{0,0,10},zero{},toward{0,0,-30},away{0,0,30};
  auto d=[&](SpatialVector lv,SpatialVector sv,float scale=1.f){return NativeDopplerFactor(l,s,lv,sv,340.f,scale);};
  assert(d(zero,zero)==1.f);assert(std::abs(*d(zero,toward)-340.f/310.f)<1.e-6f);
  assert(std::abs(*d(zero,away)-340.f/370.f)<1.e-6f);
  assert(std::abs(*d(away,zero)-370.f/340.f)<1.e-6f);
  assert(d(away,away)==1.f);assert(d(zero,{30,0,0})==1.f);assert(d(away,toward,0.f)==1.f);
  assert(d(zero,{0,0,-10000})==2.f);assert(!d(zero,zero,-1.f));
  assert(!d(zero,{0,0,std::numeric_limits<float>::infinity()}));
  assert(NativeDopplerFactor(l,l,away,toward,340,1)==1.f);
  assert(ComposeSpatialPitch(2,.5f,1.5f,0,16)==1.f);
  assert(ComposeSpatialPitch(2,.5f,1.5f,1,16)==1.5f);
  assert(ComposeSpatialPitch(2,.5f,1.5f,.5f,16)==1.25f);
  assert(ComposeSpatialPitch(16,1,2,1,16)==16.f);
  assert(ComposeSpatialPitch(.0625f,1,.5f,1,16)==.0625f);
  const auto original=[&](SpatialVector lv,SpatialVector sv,uint32_t dt=100,float gs=1,float ds=1,float cs=1,bool head=false){
    return OriginalUsaDopplerSingle(l,s,lv,sv,head,dt,gs,ds,cs);};
  assert(original(zero,zero)==1.f);
  assert(std::abs(*original(zero,toward)-370.f/340.f)<1.e-6f);
  assert(std::abs(*original(zero,away)-310.f/340.f)<1.e-6f);
  assert(original(away,away)==1.f&&original(away,zero,100,1,1,1,true)==1.f);
  assert(std::abs(*original(zero,toward,100,2,3,.5f)-1050.f/1020.f)<1.e-6f);
  assert(original(zero,away,100,0)==1.f);
  assert(original(zero,{0,0,1000},1)==1.e-6f);
  assert(!original(zero,zero,100,1,0));
  assert(ComposeSpatialPitch(1,1,4,1,16)==4.f);
  assert(ComposeSpatialPitch(1,1,1.e-6f,1,16)==.0625f);
  assert(!ComposeSpatialPitch(16,2,1,1,16));assert(!ComposeSpatialPitch(1,1,1,1,0));
}
