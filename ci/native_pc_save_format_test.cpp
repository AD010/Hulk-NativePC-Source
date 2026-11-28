#include "../native_port/src/native_pc_save_format.h"
#include "../native_port/src/native_pc_save_known_tables.h"
#include "../native_port/src/native_pc_progress_store.h"
#include <cstdlib>
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
int main(int argc,char** argv) {
  // Test rejection even when CI has no private saves.
  for(size_t n:{0u,8u,24u,4096u}) {
    std::vector<std::byte> invalid(n);
    bool rejected=false;
    try{(void)DecodeNativeProgress(invalid);}
    catch(const std::invalid_argument&){rejected=true;}
    if(!rejected)std::abort();
  }
  // Optional private owner samples, never required or published by CI.
  if(argc==3) {
    for(int i=1;i<=2;++i) {
      const auto kind=i==1?Kind::XboxBigEndian:Kind::RetailPcLittleEndian;
      const auto result=Read(argv[i],kind);
      assert(result.verified_table_count==5&&result.verified_field_count==853);
      assert(!result.pc_payload_conversion_ready);
      std::ifstream input(argv[i],std::ios::binary|std::ios::ate);
      if(!input)std::abort();
      const auto length=input.tellg();
      if(length<=0||static_cast<uint64_t>(length)>known_detail::maximum)std::abort();
      std::vector<std::byte> source(static_cast<size_t>(length));
      input.seekg(0);
      if(!input.read(reinterpret_cast<char*>(source.data()),static_cast<std::streamsize>(length)))std::abort();
      const auto native=EncodeNativeProgress(DecodeKnownTables(source,kind));
      if(EncodeKnownTables(DecodeNativeProgress(native))!=source)std::abort();
      for(size_t j=0;j<native.size();j+=97){
        auto bad=native;bad[j]^=std::byte{1};
        bool rejected=false;
        try{(void)DecodeNativeProgress(bad);}
        catch(const std::invalid_argument&){rejected=true;}
        if(!rejected)std::abort();
      }
    }
  } else assert(argc==1);
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
    const auto envelope=DecodeEnvelope(b,kind);
    assert(envelope.fields.size()==25 && envelope.fields.front().id==100 &&
           envelope.fields.back().raw_value==25);
    assert(EncodeEnvelope(envelope)==b);
    bool unknown_schema=false;
    try{(void)DecodeKnownTables(b,kind);}
    catch(const std::invalid_argument&){unknown_schema=true;}
    assert(unknown_schema);
    auto invalid=envelope;
    invalid.fields[2].id=invalid.fields[1].id;
    bool invalid_rejected=false;
    try{(void)EncodeEnvelope(invalid);}
    catch(const std::invalid_argument&){invalid_rejected=true;}
    assert(invalid_rejected);
    b[80]=std::byte{0x8a};
    bool rejected=false;
    try{(void)Inspect(b,kind);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
  }
  // Both observed formats share 242 leading IDs but not the value data.
  // The same-platform codec must preserve unknown headers and tail bytes.
  for(bool big:{false,true}) {
    std::vector<std::byte> b(4096);
    const size_t start=big?20u:16u;
    Put(b,4,4000,big);
    Put(b,8,0x12345678u,big);
    for(unsigned i=0;i<242;++i) {
      Put(b,start+8*i,10u+i*10u,big);
      Put(b,start+8*i+4,0xff000000u+i,big);
    }
    for(size_t i=start+242u*8u+4u;i<b.size();++i)
      b[i]=std::byte(i&255u);
    Put(b,0,Crc(b),big);
    const auto kind=big?Kind::XboxBigEndian:Kind::RetailPcLittleEndian;
    const auto envelope=DecodeEnvelope(b,kind);
    assert(envelope.fields.size()==242);
    assert(envelope.fields.back().raw_value==0xff0000f1u);
    assert(EncodeEnvelope(envelope)==b);
    bool unknown_schema=false;
    try{(void)DecodeKnownTables(b,kind);}
    catch(const std::invalid_argument&){unknown_schema=true;}
    assert(unknown_schema);
  }
}
