#pragma once
// USA EoR VM owner identity, reconstructed from the original interpreter
// and the user's private mapped game memory. No heap address is hardcoded.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hulk::eor::vm {
enum class PausePage : uint8_t { Unknown, Settings, GameSettings, AudioSettings, CameraSettings };
struct NativeOwner {
  uint32_t object=0, definition=0, key=0;
  PausePage page=PausePage::Unknown;
};

// Read takes (guest address, byte buffer, length). All pointer arithmetic
// is checked before invoking the guest-memory reader. Do not identify a
// Pause event by the global state_r registration alone.
template<class Read>
NativeOwner ResolveNativeOwner(uint32_t lr,uint32_t vm,uint32_t dispatcher_vm,
    uint32_t native_descriptor,uint32_t context,Read&& read,
    uint32_t expected_handler=0x8264C798u) noexcept {
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
  if(!handler || *handler!=expected_handler || !depth || *depth>512 ||
     !owner || !*owner)return result;
  const auto stack_owner=word(vm,4ull*(2093ull+*depth));
  if(!stack_owner || *stack_owner!=*owner)return result;
  const auto resource=word(*owner,124);
  if(!resource || !*resource)return result;
  const auto definition=word(*resource,8);
  if(!definition || !*definition)return result;
  const auto vtable=word(*definition,0),key=word(*definition,12);
  if(!vtable || *vtable!=0x8206A4D0u || !key)return result;
  // All four real pause-page names and CRC keys were verified against
  // the loaded USA definition records. In particular the audio and camera
  // callbacks execute in separate page owners, not only GameSettings.
  std::string_view expected;
  PausePage resolved=PausePage::Unknown;
  switch(*key) {
    case 0xD7A9E027u:expected="uipage_pause_settings";resolved=PausePage::Settings;break;
    case 0x04AA3A30u:expected="uipage_pause_gamesettings";resolved=PausePage::GameSettings;break;
    case 0xDD663BE3u:expected="uipage_pause_audiosettings";resolved=PausePage::AudioSettings;break;
    case 0x141ED2A4u:expected="uipage_pause_camerasettings";resolved=PausePage::CameraSettings;break;
    default:return result;
  }
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
  result.page=resolved;
  return result;
}
// The two registered native option thunks tail-call their original PPC
// setters. Verify the live interpreter owner, source instructions and VM
// boolean argument before accepting a persistent Windows option change.
struct OriginalCameraMenuChange {bool yaw=false,value=false,subtitles=false;};
template<class Read>
std::optional<OriginalCameraMenuChange> CaptureCameraMenuChange(
    uint32_t method,uint32_t lr,uint32_t vm,uint32_t dispatcher_vm,
    uint32_t descriptor,uint32_t context,Read&& read) noexcept {
  const bool yaw=method==0x82339A08u;
  const bool subtitles=method==0x82339A30u;
  if(!yaw && !subtitles && method!=0x823399E8u)return {};
  const uint32_t thunk=subtitles?0x8218D470u:yaw?0x821204B0u:0x8217E250u;
  const auto pinned=[&](uint32_t address,size_t length,uint64_t expected) {
    std::array<uint8_t,36> bytes{};
    if(length>bytes.size() || !read(address,bytes.data(),length))return false;
    uint64_t hash=0xcbf29ce484222325ull;
    for(size_t i=0;i<length;++i)hash=(hash^bytes[i])*0x100000001b3ull;
    return hash==expected;
  };
  if(!pinned(thunk,16,subtitles?0xc3d92cd101da085dull:yaw?0xa5cf706c175e0715ull:0xac8ad2efc6d2e862ull) ||
     !pinned(method,(subtitles||yaw)?36u:32u,subtitles?0x0a943a2be4f9fa55ull:yaw?0x1a17b8e895bd37b0ull:0x620fae1953ca8fe3ull))return {};
  const auto page=ResolveNativeOwner(lr,vm,dispatcher_vm,descriptor,context,read,thunk);
  if(page.page==PausePage::Unknown || page.page==PausePage::AudioSettings ||
     (subtitles && page.page==PausePage::CameraSettings))return {};
  const auto word=[&](uint32_t address,uint32_t offset)->std::optional<uint32_t> {
    if(!address || uint64_t(address)+offset>UINT32_MAX-3u)return {};
    std::array<uint8_t,4> raw{};
    if(!read(address+offset,raw.data(),raw.size()))return {};
    return uint32_t(raw[0])<<24|uint32_t(raw[1])<<16|uint32_t(raw[2])<<8|raw[3];
  };
  const auto frame=word(vm,0);
  const auto input=frame?word(*frame,0):std::nullopt;
  const auto nested=input?word(*input,8):std::nullopt;
  const auto owner=nested?word(*nested,0):std::nullopt;
  uint8_t value=0,previous=0;
  if(!owner || !*owner || *owner>UINT32_MAX-12u ||
     !read(*owner+12u,&value,1) || value>1 ||
     !read(subtitles?0x82E53B3Eu:yaw?0x82E53B3Du:0x82E53B3Cu,&previous,1) ||
     previous>1 || previous==value)return {};
  return OriginalCameraMenuChange{yaw,value!=0,subtitles};
}
// Restore the saved host subtitle preference only at the genuine original
// pause-page getter. The original PPC still produces the returned VM value.
template<class Read>
bool VerifySubtitleGetter(uint32_t lr,uint32_t vm,uint32_t dispatcher_vm,
    uint32_t descriptor,uint32_t context,Read&& read) noexcept {
  const auto pin=[&](uint32_t address,size_t length,uint64_t expected) {
    std::array<uint8_t,32> bytes{};
    if(!read(address,bytes.data(),length))return false;
    uint64_t hash=0xcbf29ce484222325ull;
    for(size_t i=0;i<length;++i)hash=(hash^bytes[i])*0x100000001b3ull;
    return hash==expected;
  };
  if(!pin(0x82146A10u,16,0x42cf88a419a2c655ull) ||
     !pin(0x82338A38u,28,0x280b3f5d0b51d1feull))return false;
  const auto page=ResolveNativeOwner(lr,vm,dispatcher_vm,descriptor,context,read,0x82146A10u).page;
  return page==PausePage::GameSettings || page==PausePage::Settings;
}
} // namespace hulk::eor::vm
