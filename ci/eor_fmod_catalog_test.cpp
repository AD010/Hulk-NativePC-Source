#include "../native_port/src/eor_fmod_catalog.h"
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>
using namespace hulk::eor;
namespace {
void W(std::vector<std::byte>& bytes,uint32_t value) {
  for(int i=3;i>=0;--i)bytes.push_back(std::byte((value>>(8*i))&255));
}
template<class F> void MustThrow(F&& call) {
  bool rejected=false;
  try{call();}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
}
}
int main() {
  const std::string name="synthetic_event";
  const Resource resource{ResourceHash(name),4,25,0};
  std::vector<std::byte> bytes(29);
  W(bytes,1);W(bytes,resource.hash);W(bytes,4);W(bytes,25);
  for(char value:name)bytes.push_back(std::byte(static_cast<unsigned char>(value)));
  bytes.push_back(std::byte{0});W(bytes,7);W(bytes,12);W(bytes,17);W(bytes,0);
  auto parsed=fmod::DecodeCatalog(bytes,std::span<const Resource>(&resource,1));
  assert(parsed.size()==1 && parsed[0].identifier==name && parsed[0].opaque[2]==17);
  auto changed=bytes;changed[changed.size()-16]=std::byte{0x55};
  // Different opaque owner metadata is preserved, NOT misread as a shader or hash.
  assert(fmod::DecodeCatalog(changed,std::span<const Resource>(&resource,1)).size()==1);
  changed=bytes;changed[33]=std::byte{0xff};
  MustThrow([&]{(void)fmod::DecodeCatalog(changed,std::span<const Resource>(&resource,1));});
  changed=bytes;changed.back()=std::byte{1};
  MustThrow([&]{(void)fmod::DecodeCatalog(changed,std::span<const Resource>(&resource,1));});
}
