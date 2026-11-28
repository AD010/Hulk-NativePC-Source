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
  Check(!DecodeNativeMetadata(nullptr,14));
}
