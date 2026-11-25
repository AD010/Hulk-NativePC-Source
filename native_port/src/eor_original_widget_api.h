#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace hulk::eor::ui {
// Recovered from the exact USA XEX SHA256 346a7369...b11f0.
// These are original engine entrypoints, not XDK or guessed widget offsets.
struct WidgetCodePin { uint32_t address, length; uint64_t fnv; };
inline constexpr std::array<WidgetCodePin,10> kWidgetCodePins{{
  {0x8264D918u,80,0x98767A43719C78C7ull}, // FindChild_r VM wrapper
  {0x825B8270u,72,0xADC22630C7D4B14Aull}, // direct FindChild(parent, symbol)
  {0x8264CB80u,276,0x989AF07236D42A4Dull}, // CreateInstanceWithOrientation_dsmbp
  {0x825BDEE0u,92,0x1EE5A46A66211E1Eull}, // create, attach, initialize
  {0x8260AF28u,108,0x6004D584B9EBCD5Dull}, // resource instantiation
  {0x8262EAC0u,124,0xA54C39FD2D7EC46Eull}, // instance name selection
  {0x8262E728u,508,0x402617E6147C10D0ull}, // construct, orient, create children
  {0x825BC430u,128,0x12C18FF1A3A301B0ull}, // parent registration
  {0x8271D998u,232,0x1C558B55CAC8DEDEull}, // SetTextWithString_s wrapper
  {0x8271D340u,208,0x1AA6DAD8D2272D14ull}, // native UTF-16 setter
}};
inline constexpr uint32_t kMenuItemTemplate=0x66406D09u;
inline constexpr uint32_t kTextTemplate=0x6D4F7498u;
inline constexpr uint32_t kTextChild=0x8A901DCFu;
inline constexpr uint32_t kSelectorChild=0x50A17E11u;
inline constexpr uint32_t kSettingsDefinition=0xD7A9E027u;
inline constexpr uint32_t kGameSettingsDefinition=0x04AA3A30u;

inline constexpr uint32_t WidgetSymbol(std::string_view name) noexcept {
  uint32_t crc=0xffffffffu;
  for(char c:name) {
    auto ch=static_cast<unsigned char>(c);
    if(ch>='a' && ch<='z')ch=static_cast<unsigned char>(ch-'a'+'A');
    crc^=ch;
    for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
  }
  return ~crc;
}
template<class Read>
std::optional<uint32_t> WidgetWord(uint32_t address,uint32_t offset,Read& read) {
  if(!address || uint64_t(address)+offset>UINT32_MAX-3u)return {};
  std::array<uint8_t,4> b{};
  if(!read(address+offset,b.data(),b.size()))return {};
  return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];
}
template<class Read>
bool VerifyOriginalWidgetAbi(Read& read) {
  std::array<uint8_t,508> bytes{};
  for(const auto& pin:kWidgetCodePins) {
    if(!read(pin.address,bytes.data(),pin.length))return false;
    uint64_t h=0xcbf29ce484222325ull;
    for(size_t i=0;i<pin.length;++i)h=(h^bytes[i])*0x100000001b3ull;
    if(h!=pin.fnv)return false;
  }
  constexpr std::array<std::array<uint32_t,3>,3> registry{{
    {{0x82E2F870u,0x8206C56Cu,0x8264D918u}},
    {{0x82E2F3E8u,0x8206CB10u,0x8264CB80u}},
    {{0x82E31F3Cu,0x820787B8u,0x8271D998u}},
  }};
  for(const auto& entry:registry)
    if(WidgetWord(entry[0],0,read)!=entry[1] || WidgetWord(entry[0],4,read)!=entry[2])return false;
  return true;
}
struct OriginalWidgetIdentity {uint32_t object=0,parent=0,symbol=0,definition=0;};
template<class Read>
std::optional<OriginalWidgetIdentity> ReadOriginalWidget(uint32_t object,Read& read) {
  const auto parent=WidgetWord(object,116,read),symbol=WidgetWord(object,12,read),
             resource=WidgetWord(object,124,read);
  if(!parent || !symbol || !*symbol || !resource || !*resource)return {};
  const auto def=WidgetWord(*resource,8,read);
  if(!def || !*def || WidgetWord(*def,0,read)!=0x8206A4D0u)return {};
  const auto key=WidgetWord(*def,12,read);
  if(!key || !*key)return {};
  return OriginalWidgetIdentity{object,*parent,*symbol,*key};
}
// invoke executes a verified original engine routine on the owning VM/game
// thread and returns r3; scratch addresses belong to that same live engine.
// No heap pointer is hardcoded and no original widget is recreated in ImGui.
template<class Read,class Invoke>
class OriginalWidgetApi {
 public:
  OriginalWidgetApi(Read& read,Invoke& invoke):read_(read),invoke_(invoke),verified_(VerifyOriginalWidgetAbi(read)){}
  bool Verified() const noexcept {return verified_;}
  std::optional<OriginalWidgetIdentity> Find(uint32_t parent,uint32_t symbol) {
    if(!verified_ || !symbol || !ReadOriginalWidget(parent,read_))return {};
    const std::array<uint32_t,2> args{parent,symbol};
    const auto child=invoke_(0x825B8270u,std::span<const uint32_t>(args));
    if(!child || !*child)return {};
    auto id=ReadOriginalWidget(*child,read_);
    if(!id || id->parent!=parent || id->symbol!=symbol)return {};
    return id;
  }
  // orientation is a caller-owned genuine 64-byte EoR transform, not a
  // Win32/ImGui rectangle. The native constructor performs attachment.
  std::optional<OriginalWidgetIdentity> CreateMenuRow(uint32_t page,uint32_t parent,
      uint32_t symbol,uint32_t orientation) {
    if(!verified_ || !symbol || !orientation || (orientation&15u) ||
       uint64_t(orientation)+64>uint64_t(UINT32_MAX)+1u)return {};
    const auto root=ReadOriginalWidget(page,read_);
    if(!root || (root->definition!=kSettingsDefinition && root->definition!=kGameSettingsDefinition))return {};
    auto owner=ReadOriginalWidget(parent,read_);
    if(!owner || (parent!=page && owner->parent!=page))return {};
    std::array<uint8_t,64> matrix{};
    if(!read_(orientation,matrix.data(),matrix.size()))return {};
    for(size_t i=0;i<matrix.size();i+=4)
      if((matrix[i]&0x7fu)==0x7fu && (matrix[i+1]&0x80u))return {};
    if(matrix[60]!=0x3f || matrix[61]!=0x80 || matrix[62] || matrix[63])return {};
    // A lookup failure is NOT absence. Do not create a duplicate when
    // the live child table already contains a foreign or stale owner.
    const std::array<uint32_t,2> lookup{parent,symbol};
    const auto existing=invoke_(0x825B8270u,std::span<const uint32_t>(lookup));
    if(!existing)return {};
    if(*existing) {
      const auto id=ReadOriginalWidget(*existing,read_);
      if(!id || id->parent!=parent || id->symbol!=symbol ||
         id->definition!=kMenuItemTemplate)return {};
      return id;
    }
    const std::array<uint32_t,6> args{parent,kMenuItemTemplate,0,orientation,1,symbol};
    const auto child=invoke_(0x825BDEE0u,std::span<const uint32_t>(args));
    if(!child || !*child)return {};
    const auto result=Find(parent,symbol);
    if(!result || result->object!=*child || result->definition!=kMenuItemTemplate)return {};
    return result;
  }
  // Original SetTextWithString_s converts its VM string to a NUL-terminated
  // UTF-16 buffer then calls 8271D340. The engine copies that string.
  bool SetRowText(uint32_t row,uint32_t text_address,size_t code_units) {
    if(!verified_ || !text_address || (text_address&1u) || !code_units || code_units>255 ||
       uint64_t(text_address)+2u*(code_units+1)>uint64_t(UINT32_MAX)+1u)return false;
    const auto owner=ReadOriginalWidget(row,read_);
    if(!owner || owner->definition!=kMenuItemTemplate)return false;
    const auto text=Find(row,kTextChild);
    if(!text || text->definition!=kTextTemplate)return false;
    std::array<uint8_t,512> buffer{};
    if(!read_(text_address,buffer.data(),2u*(code_units+1)))return false;
    for(size_t i=0;i<code_units;++i)if(!buffer[2*i] && !buffer[2*i+1])return false;
    if(buffer[2*code_units] || buffer[2*code_units+1])return false;
    const std::array<uint32_t,2> args{text->object,text_address};
    return invoke_(0x8271D340u,std::span<const uint32_t>(args)).has_value();
  }
 private:
  Read& read_; Invoke& invoke_; bool verified_;
};
} // namespace hulk::eor::ui
