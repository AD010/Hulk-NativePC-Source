// Hulk NativeXRenderer full configuration parser and hook address registry.
#include "native_x_hulk_profile.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <sstream>
#include <string_view>
#include <cctype>

namespace hulk::nx {
namespace {
const GameProfile* g_active = nullptr;
std::string Trim(std::string s) {
  const auto p=s.find_first_not_of(" \t\r\n");
  if(p==s.npos) return {};
  const auto e=s.find_last_not_of(" \t\r\n");
  return s.substr(p,e-p+1);
}
bool Number(std::string_view value, uint32_t& out) {
  int base=10;
  if (value.size()>2 && value[0]=='0' && (value[1]=='x'||value[1]=='X')) {value.remove_prefix(2);base=16;}
  if(value.empty())return false;
  unsigned long long n=0;
  const auto [p,err]=std::from_chars(value.data(),value.data()+value.size(),n,base);
  if(err!=std::errc{}||p!=value.data()+value.size()||n>UINT32_MAX)return false;
  out=static_cast<uint32_t>(n);return true;
}
bool Compound(std::string value,std::array<uint32_t,3>& out, unsigned n) {
  for (unsigned i=0;i<n;++i) {
    const auto comma=value.find(',');
    if (i+1<n && comma==value.npos)return false;
    const auto part=Trim(value.substr(0,comma));
    if (!Number(part,out[i]))return false;
    if(i+1<n)value.erase(0,comma+1);
    else if(comma!=value.npos)return false;
  }
  return true;
}
// Symbol names must match native_x_renderer/include/native_x_renderer/calls.inc.
constexpr std::pair<std::string_view,native_x_renderer::Call> kCalls[]={
#define NATIVE_X_RENDERER_CALL(name,arguments,flags) {#name,native_x_renderer::Call::k##name},
#include <native_x_renderer/calls.inc>
#undef NATIVE_X_RENDERER_CALL
};
}
void Install(const GameProfile* p) {g_active=p;}
native_x_renderer::Call Lookup(uint32_t guest) {
  if(!g_active)return native_x_renderer::Call::kCount;
  const auto iter=g_active->calls.find(guest);
  return iter==g_active->calls.end()?native_x_renderer::Call::kCount:iter->second;
}
bool LoadGameProfileStream(std::istream& f, const std::string& origin, GameProfile& out) {
  out.calls.clear();out.profile=native_x_renderer::Profile{};out.experimental=false;
  std::string section,line;
  unsigned lineno=0;
  bool xdk_found=false;
  auto fail=[&](std::string why){out.error=origin+":"+std::to_string(lineno)+": "+why;return false;};
  while(std::getline(f,line)) {
    ++lineno;
    // Comments use # or ; outside a value; neither is a meaningful part of a numeric field.
    auto comment=line.find_first_of("#;");
    if(comment!=line.npos)line.resize(comment);
    line=Trim(line);
    if(line.empty())continue;
    if(line.front()=='['&&line.back()==']'){section=Trim(line.substr(1,line.size()-2));continue;}
    auto eq=line.find('=');if(eq==line.npos)return fail("expected key=value");
    const auto key=Trim(line.substr(0,eq)),str=Trim(line.substr(eq+1));
    if(key.empty()||str.empty())return fail("empty key or value");
    if(section=="profile") {
      if(key=="experimental") { if(str!="0" && str!="1")return fail("experimental must be 0 or 1"); out.experimental=(str=="1"); }
      else if(key=="xdk_version") {out.version=str; xdk_found=true;}
      else if(key=="game")out.name=str;
      else if(key=="output_width"||key=="output_height") {
        uint32_t v;if(!Number(str,v)||v<1||v>16384)return fail("invalid output dimensions");
        if(key=="output_width")out.profile.output_width=v;else out.profile.output_height=v;
      } else return fail("unknown profile field "+key);
      continue;
    }
    if(section=="hooks") {
      uint32_t address;
      if(!Number(key,address)||address<0x80000000u||address>0x8FFFFFFFu)return fail("invalid guest hook address "+key);
      const auto iter=std::find_if(std::begin(kCalls),std::end(kCalls),[&](const auto& x){return x.first==str;});
      if(iter==std::end(kCalls))return fail("unknown NativeXRenderer call "+str);
      if(!out.calls.emplace(address,iter->second).second)return fail("duplicate hook address "+key);
      continue;
    }
    if(section=="device") {
      uint32_t v;
      if (!Number(str,v))return fail("invalid device offset "+key);
      auto& d=out.profile.device;
#define D_FIELD(name) if(key==#name){d.name=v;continue;}
      D_FIELD(vs_constants_dirty) D_FIELD(ps_constants_dirty) D_FIELD(fetch_constants_dirty)
      D_FIELD(fetch_dirty_slot0_bit) D_FIELD(command_buffer_write) D_FIELD(fetch_constants)
      D_FIELD(vs_float_constants) D_FIELD(ps_float_constants) D_FIELD(bool_loop_constants)
      D_FIELD(fence_counter) D_FIELD(write_back_block) D_FIELD(flags)
      D_FIELD(vertex_declaration) D_FIELD(index_buffer) D_FIELD(render_targets)
      D_FIELD(depth_stencil) D_FIELD(vertex_buffers) D_FIELD(textures) D_FIELD(viewport)
      D_FIELD(pixel_shader) D_FIELD(vertex_shader) D_FIELD(predication_mask)
      D_FIELD(tile_count) D_FIELD(tile_rects) D_FIELD(inline_draw_end)
#undef D_FIELD
      return fail("unknown device field "+key);
    }
    if(section=="objects") {
      auto& o=out.profile.objects;
      if(key=="surface_width_minus_one"||key=="surface_height_minus_one") {
        std::array<uint32_t,3> c{};
        if(!Compound(str,c,2)||c[0]>31||c[1]<1||c[1]>31||c[0]+c[1]>32)return fail("invalid size bitfield");
        auto& b=(key=="surface_width_minus_one"?o.surface_width_minus_one:o.surface_height_minus_one);
        b.shift=uint8_t(c[0]);b.bits=uint8_t(c[1]);continue;
      }
      uint32_t v;if(!Number(str,v))return fail("invalid object offset "+key);
#define O_FIELD(name) if(key==#name){o.name=v;continue;}
      O_FIELD(texture_fetch) O_FIELD(surface_info) O_FIELD(surface_color_depth_info)
      O_FIELD(surface_size) O_FIELD(buffer_address) O_FIELD(buffer_size)
#undef O_FIELD
      return fail("unknown object field "+key);
    }
    if(section=="registers") {
      // group_0..group_7 = offset,first_xenos_register,count
      if(key.size()!=7||key.substr(0,6)!="group_"||key[6]<'0'||key[6]>'7')return fail("invalid register group key "+key);
      std::array<uint32_t,3> c{};
      if(!Compound(str,c,3)||c[1]>0xFFFF||c[2]>0xFFFF)return fail("invalid register group triple");
      out.profile.device.register_groups[key[6]-'0']={c[0],static_cast<uint16_t>(c[1]),static_cast<uint16_t>(c[2])};
      continue;
    }
    return fail("unknown section ["+section+"]");
  }
  out.BindStrings();
  if(!xdk_found||out.version!="2.0.6534")return fail("profile XDK version must match Hulk: 2.0.6534");
  const auto& d=out.profile.device;const auto& o=out.profile.objects;
  const auto required=std::array<uint32_t,9>{d.vs_constants_dirty,d.ps_constants_dirty,d.fetch_constants_dirty,
    d.command_buffer_write,d.fetch_constants,d.vs_float_constants,d.ps_float_constants,
    d.bool_loop_constants,d.render_targets};
  if(std::find(required.begin(),required.end(),native_x_renderer::kUnknown)!=required.end() ||
     d.fetch_dirty_slot0_bit<31||d.fetch_dirty_slot0_bit>63)return fail("incomplete or invalid device layout (not enabling native GPU)");
  if(o.texture_fetch==native_x_renderer::kUnknown||o.surface_info==native_x_renderer::kUnknown||
     o.surface_color_depth_info==native_x_renderer::kUnknown||o.surface_size==native_x_renderer::kUnknown||
     !o.surface_width_minus_one.bits||!o.surface_height_minus_one.bits)return fail("incomplete object layout");
  // Verified directly from BOTH user-owned Hulk PPC binaries, XDK 2.0.6534.
  // The old borrowed XDK 7978 layout used RT0=12448 / Z=12464, which
  // actually points at RT2 / a different field in Hulk. Never silently
  // activate host drawing against that corrupt layout, even if all hooks
  // exist and someone changed experimental=0.
  if (d.command_buffer_write != 48 || d.render_targets != 12440 ||
      d.depth_stencil != 12456 || d.flags != 10940 ||
      d.inline_draw_end != 13444 || d.vertex_declaration != 11820 ||
      d.index_buffer != 12436 || d.vertex_buffers != 12460 ||
      d.textures != 12544 || d.viewport != 12648 ||
      d.pixel_shader != 12692 || d.vertex_shader != 12696 ||
      d.predication_mask != 12708 || d.tile_count != 12748 ||
      d.tile_rects != 12752)
    return fail("XDK 6534 observed device layout mismatch: verify all resource/shader/RT offsets against Hulk PPC before native GPU");
  bool draw=false,present=false,resolve=false,targets=false;
  for(const auto&[_,type]:out.calls){
    using C=native_x_renderer::Call;
    if(type==C::kDrawVertices||type==C::kDrawIndexedVertices||type==C::kDrawVerticesUP)draw=true;
    if(type==C::kPresent)present=true;
    if(type==C::kResolve)resolve=true;
    if(type==C::kSetRenderTarget)targets=true;
  }
  // Experimental/borrowed XDK 7978/5426 fields are NOT valid Hulk XDK
  // 6534 evidence. Activating them turns Xenos host drawing off even if
  // the game has no valid native Draw/Resolve/Present event stream; this
  // produced a reproducible black window in Hulk run 050.
  if (out.experimental)
    return fail("unverified XDK 2.0.6534 layout: experimental profiles cannot disable Xenos rendering");
  if (!draw || !present) return fail("required Draw and Present entrypoints missing");
  // ElGuslml/NativeXRenderer src/renderer.cpp (D72) explicitly ignores
  // setter arguments: draw/resolve snapshots use XDK device shadow state.
  // Hulk can inline SetTexture, so requiring a standalone method here was
  // an invalid barrier to native Vulkan. Resolve and RT changes must be
  // tracked, and their entrypoints have been verified in Hulk PPC.
  if (!resolve || !targets)
    return fail("required Resolve/SetRenderTarget XDK hooks missing");
  out.error.clear();return true;
}

bool LoadGameProfile(const std::filesystem::path& path, GameProfile& out) {
  std::ifstream f(path);
  if (!f) { out.error="missing profile: "+path.string();return false; }
  return LoadGameProfileStream(f, path.string(), out);
}

bool LoadGameProfileFromText(std::string_view text, GameProfile& out) {
  std::istringstream f{std::string(text)};
  return LoadGameProfileStream(f, "embedded Hulk 6534 profile", out);
}
} // namespace hulk::nx
