#include "native_save_metadata.h"
#include <cstdlib>
using namespace hulk::save;
static void Check(bool b){if(!b)std::abort();}
int main(){
  Check(IsHulkSaveSlot("HULK.SAV")&&IsHulkSaveSlot("hulk.sav")&&!IsHulkSaveSlot("HULK.SAV/..")&&!IsHulkSaveSlot("HULK2.SAV"));
  for(const auto& name:{std::u16string{},std::u16string(u"Hulk"),std::u16string(u"\u0413\u0435\u0440\u043e\u0439 \U0001f49a"),std::u16string(127,u'x')}){
    const auto encoded=EncodeNativeMetadata(name);Check(encoded.has_value());
    Check(DecodeNativeMetadata(encoded->data(),encoded->size())==name);
    for(size_t i=0;i<encoded->size();++i){auto bad=*encoded;bad[i]^=1;Check(!DecodeNativeMetadata(bad.data(),bad.size()));}
    for(size_t n=0;n<encoded->size();++n)Check(!DecodeNativeMetadata(encoded->data(),n));
  }
  Check(!EncodeNativeMetadata(std::u16string(128,u'x')));
  Check(!EncodeNativeMetadata(std::u16string{char16_t(0xd800)}));
  Check(!EncodeNativeMetadata(std::u16string{char16_t(0xdc00)}));
  Check(!EncodeNativeMetadata(std::u16string{u'a',0,u'b'}));
  const auto le=EncodeNativeMetadata(u"\u1234");Check(le&&(*le)[10]==0x34&&(*le)[11]==0x12);
  Check(NativeMetadataCrc(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926u);
  // Known CRC framing of the original USA progress, not an invented
  // interpretation of mission, unlock or statistics fields.
  std::vector<uint8_t> progress(1024,0);
  for(size_t i=12;i<progress.size();++i)progress[i]=static_cast<uint8_t>(i);
  const auto crc=NativeMetadataCrc(progress.data()+12,progress.size()-12);
  for(unsigned i=0;i<4;++i)progress[i]=static_cast<uint8_t>(crc>>(24-8*i));
  Check(ValidLegacyProgress(progress.data(),progress.size()));
  for(size_t i=0;i<progress.size();++i){
    if(i>=4&&i<12)continue; // opaque header bytes excluded by retail CRC
    progress[i]^=1;
    Check(!ValidLegacyProgress(progress.data(),progress.size()));
    progress[i]^=1;
  }
  Check(!ValidLegacyProgress(progress.data(),progress.size()-1));
  Check(!ValidLegacyProgress(progress.data(),63));
  Check(!ValidLegacyProgress(progress.data(),kLegacyProgressMaximum+1));
  Check(!ValidLegacyProgress(nullptr,progress.size()));
  Check(!DecodeNativeMetadata(nullptr,14));
}
