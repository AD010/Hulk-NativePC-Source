#include "../native_port/src/eor_original_fog.h"
#include <cstdlib>
#include <limits>
using namespace hulk::eor::world;
static void Check(bool ok){if(!ok)std::abort();}
static FogRange R(float a,float b){return {std::bit_cast<uint32_t>(a),std::bit_cast<uint32_t>(b)};}
int main(){
  const auto s=R(100,500);
  Check(ScaleOriginalFogCopies(s,s,s,2)==R(200,1000));
  Check(ScaleOriginalFogCopies(s,s,s,4)==R(400,2000));
  Check(!ScaleOriginalFogCopies(s,s,s,1));
  Check(!ScaleOriginalFogCopies(s,s,s,5));
  Check(!ScaleOriginalFogCopies(s,s,R(200,1000),2));
  Check(!ScaleOriginalFogCopies(s,R(200,1000),R(200,1000),2));
  for(auto bad:{R(-1,500),R(500,500),R(500,100),R(0,std::numeric_limits<float>::infinity()),R(0,std::numeric_limits<float>::quiet_NaN())})
    Check(!ScaleOriginalFogCopies(bad,bad,bad,2));
  unsigned reads=0;
  auto read=[&](uint32_t,std::byte*,size_t){++reads;return false;};
  Check(!VerifyFogRenderCall(0,0x1000,0x1050,read)&&reads==0);
  Check(!VerifyFogRenderCall(0x8229a5fc,0x1000,0x1054,read)&&reads==0);
  Check(!VerifyFogRenderCall(0x8229a5fc,0x1000,0x1050,read)&&reads==1);
}
