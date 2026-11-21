#include "../native_port/src/eor_ui_original_dds.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>
using hulk::eor::ui::DecodePcTxflUi;
void W(std::vector<std::byte>& b,size_t p,uint32_t n) {
  for(size_t i=0;i<4;++i)b[p+i]=std::byte((n>>(i*8))&255);
}
bool Reject(const std::vector<std::byte>& b,const char* name) {
  try {(void)DecodePcTxflUi(b,name);}catch(const std::exception&){return true;}
  return false;
}
int main() {
  std::string name="ui_menubands_da";
  std::vector<std::byte> b(4+name.size()+1+26+128+16);
  b[0]=std::byte{'T'};b[1]=std::byte{'X'};
  b[2]=std::byte{'F'};b[3]=std::byte{'L'};
  for(size_t i=0;i<name.size();++i)b[4+i]=std::byte(uint8_t(name[i]));
  const size_t d=4+name.size()+1+26;
  b[d]=std::byte{'D'};b[d+1]=std::byte{'D'};
  b[d+2]=std::byte{'S'};b[d+3]=std::byte{' '};
  W(b,d+4,124);W(b,d+12,2);W(b,d+16,2);
  W(b,d+28,1);W(b,d+76,32);W(b,d+80,65);W(b,d+88,32);
  W(b,d+92,0x00ff0000);W(b,d+96,0x0000ff00);
  W(b,d+100,0x000000ff);W(b,d+104,0xff000000);
  b[d+128]=std::byte{0x10};b[d+129]=std::byte{0x20};
  b[d+130]=std::byte{0x30};b[d+131]=std::byte{0x40};
  const auto x=DecodePcTxflUi(b,name);
  assert(x.width==2 && x.height==2 && x.rgba.size()==16);
  assert(x.rgba[0]==0x30 && x.rgba[1]==0x20 &&
         x.rgba[2]==0x10 && x.rgba[3]==0x40);
  assert(Reject(b,"ui_menuitems_da"));
  auto bad=b;W(bad,d+80,4);assert(Reject(bad,name.c_str()));
  bad=b;W(bad,d+104,0);assert(Reject(bad,name.c_str()));
  bad=b;bad.pop_back();assert(Reject(bad,name.c_str()));
  bad=b;bad.push_back(std::byte{0});assert(Reject(bad,name.c_str()));
  bad=b;bad[0]=std::byte{'L'};assert(Reject(bad,name.c_str()));
}
