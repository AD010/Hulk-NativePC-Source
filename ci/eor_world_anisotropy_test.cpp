#include "../native_port/src/eor_world_anisotropy.h"
#include <cstdlib>
using hulk::eor::gpu::NativeWorldAnisotropy;
static void Check(bool pass){if(!pass)std::abort();}
int main(){
  for(int requested=-2;requested<=7;++requested)
    for(unsigned mask=0;mask<256;++mask){
      const bool world=mask&1,regular=mask&2,linear=mask&4,mip=mask&8,
          multiple=mask&16,base=mask&32,adjust=mask&64,point=mask&128;
      const auto result=NativeWorldAnisotropy(requested,4,8,world,regular,
          linear,mip,multiple,base,adjust,point);
      const bool eligible=mask==63 && requested>=0 && requested<=5;
      const unsigned wanted=eligible?
          (requested<=1?1u:std::min(8u,1u<<unsigned(requested-1))):4u;
      Check(result==wanted);
    }
  Check(NativeWorldAnisotropy(5,1,16,true,true,true,true,true,true,false,false)==16);
  Check(NativeWorldAnisotropy(5,1,0,true,true,true,true,true,true,false,false)==1);
}
