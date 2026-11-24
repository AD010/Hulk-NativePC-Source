#include "../src/launcher/native_camera_direct.h"
#include <vector>
#include <bit>
#include <cstdlib>
#include <iostream>
#define REQUIRE(x) do { if (!(x)) { std::cerr << "FAIL: " #x << " at line " << __LINE__ << "\n"; std::exit(1); } } while (0)
#include <cmath>
#include <cstring>
#include <iostream>
static void W(std::vector<uint8_t>& b,uint32_t a,uint32_t v){
 b[a]=uint8_t(v>>24);b[a+1]=uint8_t(v>>16);b[a+2]=uint8_t(v>>8);b[a+3]=uint8_t(v);
}
static float R(const std::vector<uint8_t>& b,uint32_t a){
 return std::bit_cast<float>(uint32_t(b[a])<<24|uint32_t(b[a+1])<<16|uint32_t(b[a+2])<<8|uint32_t(b[a+3]));
}
int main(){
 using namespace hulk::camera;
 std::vector<uint8_t> memory(2048);
 Layout c{.pointer_slot=0x80,.yaw_offset=0x24,.pitch_offset=0x28,.radians_per_count=0.01f,.pitch_min=-1.2f,.pitch_max=1.2f};
 W(memory,0x80,0x200);W(memory,0x224,std::bit_cast<uint32_t>(0.0f));W(memory,0x228,std::bit_cast<uint32_t>(0.0f));
 REQUIRE(ApplyToGuestSpan(memory.data(),memory.size(),c,10,-20));
 REQUIRE(std::abs(R(memory,0x224)-0.1f)<1.e-5f);
 REQUIRE(std::abs(R(memory,0x228)+0.2f)<1.e-5f);
 // Direct yaw/pitch must honor the same adjustable sensitivity as RawInput.
 W(memory,0x224,std::bit_cast<uint32_t>(0.0f));
 W(memory,0x228,std::bit_cast<uint32_t>(0.0f));
 REQUIRE(ApplyToGuestSpan(memory.data(),memory.size(),c,10,-20,2.0f));
 REQUIRE(std::abs(R(memory,0x224)-0.2f)<1.e-5f);
 REQUIRE(std::abs(R(memory,0x228)+0.4f)<1.e-5f);
 W(memory,0x224,std::bit_cast<uint32_t>(0.0f));
 W(memory,0x228,std::bit_cast<uint32_t>(0.0f));
 REQUIRE(ApplyToGuestSpan(memory.data(),memory.size(),c,10,-20,2.0f,0.5f));
 REQUIRE(std::abs(R(memory,0x224)-0.2f)<1.e-5f);
 REQUIRE(std::abs(R(memory,0x228)+0.1f)<1.e-5f);
 // Original menu and PC changes are reconciled per axis, not XORed.
 REQUIRE((ReconcileLookInversion({}, {false,true}, {true,false})==OriginalLookInversion{true,true}));
 REQUIRE((ReconcileLookInversion(OriginalLookInversion{true,true}, {false,true}, {true,false})==OriginalLookInversion{false,false}));
 REQUIRE((ReconcileLookInversion(OriginalLookInversion{false,false}, {true,false}, {false,true})==OriginalLookInversion{true,true}));
 REQUIRE((ReconcileLookInversion(OriginalLookInversion{true,true}, {true,true}, {false,true})==OriginalLookInversion{false,true}));
 // As in the original settings page, yaw and pitch are independent.
 for (bool invert_yaw : {false, true}) {
   for (bool invert_pitch : {false, true}) {
     for (bool profile_y : {false, true}) {
       Layout calibrated=c;
       calibrated.invert_y=profile_y;
       const auto angles=ApplyDelta({},10,20,calibrated,1.0f,0.5f,{invert_yaw,invert_pitch});
       REQUIRE(std::abs(angles.yaw-(invert_yaw ? -0.1f : 0.1f))<1.e-5f);
       REQUIRE(std::abs(angles.pitch-(profile_y!=invert_pitch ? -0.1f : 0.1f))<1.e-5f);
     }
   }
 }
 REQUIRE(ApplyToGuestSpan(memory.data(),memory.size(),c,0,10000));
 REQUIRE(std::abs(R(memory,0x228)-1.2f)<1.e-5f);
 W(memory,0x80,0xFFFFFF00u);
 REQUIRE(!ApplyToGuestSpan(memory.data(),memory.size(),c,10,0));
 std::cout<<"Native direct camera big-endian tests passed (synthetic memory)\n";
}
