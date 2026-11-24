#pragma once
// USA EoR VM owner identity, reconstructed from the original interpreter
// and the user's private mapped game memory. No heap address is hardcoded.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hulk::eor::vm {
enum class PausePage : uint8_t { Unknown, Settings, GameSettings };
struct NativeOwner {
  uint32_t object=0, definition=0, key=0;
  PausePage page=PausePage::Unknown;
};

// Read takes (guest address, byte buffer, length). All pointer arithmetic
// is checked before invoking the guest-memory reader. Do not identify a
// Pause event by the global state_r registration alone.
template<class Read>
NativeOwner ResolveNativeOwner(uint32_t lr,uint32_t vm,uint32_t dispatcher_vm,
    uint32_t native_descriptor,uint32_t context,Read&& read) noexcept {
  NativeOwner result;
  if(lr!=0x825EA158u || !vm || dispatcher_vm!=vm ||
     !native_descriptor || !context)return result;
  const auto word=[&](uint32_t base,uint64_t offset)->std::optional<uint32_t> {
    const auto address=uint64_t(base)+offset;
    if(!base || address>UINT32_MAX-3u)return {};
    std::array<uint8_t,4> b{};
    if(!read(static_cast<uint32_t>(address),b.data(),b.size()))return {};
    return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];
  };
  const auto pinned=[&](uint32_t address,size_t length,uint64_t expected) {
    std::array<uint8_t,232> b{};
    if(length>b.size() || !read(address,b.data(),length))return false;
    uint64_t hash=0xcbf29ce484222325ull;
    for(size_t i=0;i<length;++i)hash=(hash^b[i])*0x100000001b3ull;
    return hash==expected;
  };
  // 825EA070 dispatch saves original r3/r4/r5 in r31/r27/r28.
  // Immediately before bctrl, VM owner-stack[depth] equals context+20.
  // 825E9F80 and 825BBA78 obtain the current owner's definition.
  if(!pinned(0x825EA070u,0xE8u,0x0D887273A0894F1Eull) ||
     !pinned(0x825E9F80u,0x74u,0x4FE97D94F58D8A82ull) ||
     !pinned(0x825BBA78u,0x18u,0x568269E8BDC58034ull))return result;
  const auto handler=word(native_descriptor,0), depth=word(vm,32);
  const auto owner=word(context,20);
  if(!handler || *handler!=0x8264C798u || !depth || *depth>512 ||
     !owner || !*owner)return result;
  const auto stack_owner=word(vm,4ull*(2093ull+*depth));
  if(!stack_owner || *stack_owner!=*owner)return result;
  const auto resource=word(*owner,124);
  if(!resource || !*resource)return result;
  const auto definition=word(*resource,8);
  if(!definition || !*definition)return result;
  const auto vtable=word(*definition,0),key=word(*definition,12);
  if(!vtable || *vtable!=0x8206A4D0u || !key)return result;
  // The two names and CRC keys were independently present in the original
  // serialized OBJECTS.ARC and in the loaded USA definition records.
  const std::string_view expected=*key==0xD7A9E027u?"uipage_pause_settings":
      *key==0x04AA3A30u?"uipage_pause_gamesettings":"";
  if(expected.empty())return result;
  const auto tagged=word(*definition,4);
  if(!tagged || !*tagged)return result;
  uint32_t name=*tagged;
  // Exact tagged string representation from 825E9F80:
  // bit 1 = inline pointer (including its low address bits);
  // otherwise bit 0 selects direct aligned pointer or indirect string.
  if(!(name&2u)) {
    if(name&1u)name&=~1u;
    else {
      const auto indirect=word(name,0);
      if(!indirect || !*indirect)return result;
      name=*indirect;
    }
  }
  std::array<uint8_t,32> bytes{};
  if(!name || expected.size()+1>bytes.size() ||
     uint64_t(name)+expected.size()>UINT32_MAX ||
     !read(name,bytes.data(),expected.size()+1) ||
     bytes[expected.size()]!=0)return result;
  for(size_t i=0;i<expected.size();++i) {
    auto c=bytes[i];
    if(c>='A' && c<='Z')c=uint8_t(c+('a'-'A'));
    if(c!=static_cast<uint8_t>(expected[i]))return result;
  }
  result.object=*owner;result.definition=*definition;result.key=*key;
  result.page=*key==0xD7A9E027u?PausePage::Settings:PausePage::GameSettings;
  return result;
}
} // namespace hulk::eor::vm
