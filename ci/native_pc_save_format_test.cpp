#include "../native_port/src/native_pc_save_format.h"
#include <cassert>
#include <stdexcept>
#include <vector>
using namespace hulk::save::format;
namespace {
void Put(std::vector<std::byte>& b,size_t p,uint32_t word,bool big) {
  for(unsigned i=0;i<4;++i)
    b[p+i]=std::byte((word>>(8*(big?3-i:i)))&255);
}
uint32_t Crc(const std::vector<std::byte>& b) {
  uint32_t crc=0xffffffffu;
  for(size_t p=12;p<b.size();++p) {
    crc^=std::to_integer<uint8_t>(b[p]);
    for(unsigned i=0;i<8;++i)
      crc=(crc>>1)^((crc&1u)?0xedb88320u:0);
  }
  return crc^0xffffffffu;
}
}
int main() {
  for(bool big:{false,true}) {
    std::vector<std::byte> b(512);
    Put(b,4,480,big);
    const size_t start=big?20:16;
    for(unsigned i=0;i<25;++i) {
      Put(b,start+8*i,100+i*100,big);
      Put(b,start+8*i+4,i+1,big);
    }
    Put(b,0,Crc(b),big);
    const auto kind=big?Kind::XboxBigEndian:Kind::RetailPcLittleEndian;
    const auto report=Inspect(b,kind);
    assert(report.sorted_field_count==25 && !report.pc_payload_conversion_ready);
    b[80]=std::byte{0x8a};
    bool rejected=false;
    try{(void)Inspect(b,kind);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
  }
}
