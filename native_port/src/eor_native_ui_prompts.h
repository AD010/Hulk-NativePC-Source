#pragma once
// Original EoR UI texture slots, not an overlay. Original hashes are
// cross-checked in the decoded PC/Xbox360 texture indices. The label
// must come from the same NativePC binding consumed by the input bridge.
#include "native_pc_bindings.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hulk::eor::ui {
// Published by actual slot-zero input arbitration, not an assumption
// from startup configuration. Remains stable during focus loss/pause.
inline std::atomic<bool> g_pc_prompt_input_active{false};
inline void PublishPcPromptInput(bool keyboard) noexcept {
  g_pc_prompt_input_active.store(keyboard,std::memory_order_release);
}
inline bool PcPromptInputActive() noexcept {
  return g_pc_prompt_input_active.load(std::memory_order_acquire);
}
inline std::string_view PcPromptBinding(uint32_t source_hash) noexcept {
  switch(source_hash) {
    case 0x16FEB6A7u:return "hulk_bind_confirm";
    case 0x044B1949u:return "hulk_bind_back";
    case 0x3BEFAEB2u:return "hulk_bind_primary";
    case 0x8353C9D7u:return "hulk_bind_secondary";
    case 0xD9E8AD12u:return "hulk_bind_lt";
    case 0x063884F1u:return "hulk_bind_rt";
    case 0xAC9AA551u:return "hulk_bind_lb";
    case 0x734A8CB2u:return "hulk_bind_rb";
    case 0xB3951152u:return "hulk_bind_start";
    case 0xEC6B934Cu:return "hulk_bind_select";
    case 0xC76C7AB4u:return "hulk_bind_menu_up";
    case 0x9E32DFE1u:return "hulk_bind_menu_down";
    case 0x6E62BB29u:return "hulk_bind_menu_left";
    case 0xEBDDBBC2u:return "hulk_bind_menu_right";
    default:return {};
  }
}
inline std::string_view FirstPromptToken(std::string_view bindings) noexcept {
  std::string_view token;
  options::AnyBindingKey(bindings,[&](std::string_view key) {
    if(!options::IsBindingToken(key))return false;
    token=key;return true;
  });
  return token;
}
inline std::string PromptLabel(std::string_view token) {
  if(!options::IsBindingToken(token))return {};
  if(token=="MouseLeft")return "LMB";
  if(token=="MouseRight")return "RMB";
  if(token=="MouseMiddle")return "MMB";
  std::string label(token);
  for(auto& c:label)if(c>='a' && c<='z')c=char(c-'a'+'A');
  return label;
}
inline std::string_view KnownPcPromptAsset(std::string_view token) noexcept {
  // These names are actual retail PC archive entries, not invented hashes.
  if(token=="Space")return "ui_key_space_da";
  if(token=="MouseLeft")return "ui_mouse_leftbutton_da";
  if(token=="MouseRight")return "ui_mouse_rightbutton_da";
  if(token=="MouseMiddle")return "ui_mouse_middlebutton_da";
  if(token=="Enter")return "ui_key_enter_da";
  if(token=="Tab")return "ui_key_tab_da";
  if(token=="Esc")return "ui_key_escape_da";
  if(token=="F")return "ui_key_f_da";
  if(token=="M")return "ui_key_m_da";
  if(token=="1")return "ui_key_1_da";
  if(token=="2")return "ui_key_2_da";
  if(token=="3")return "ui_key_3_da";
  if(token=="4")return "ui_key_4_da";
  return {};
}
inline constexpr std::array<std::array<uint8_t,7>,36> kKeyFont{{
  {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
  {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
  {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {31,4,4,4,4,4,31},
  {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
  {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
  {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
  {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
  {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
  {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
  {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
  {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
  {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
  {14,17,17,15,1,1,14}
}};
// A readable keycap when the user's original PC art is unavailable.
// Pixel dimensions are identical to the original Xbox glyph allocation,
// so no shader/UV/layout assumption changes. Transparent outside the key.
inline std::vector<uint8_t> RasterPcKeycap(std::string_view token,
    uint32_t width,uint32_t height) {
  const auto label=PromptLabel(token);
  if(label.empty() || width<32 || height<16 || width>256 || height>256)return {};
  const auto letters=static_cast<uint32_t>(label.size());
  const auto units=letters*6u-1u;
  const auto scale=std::min((width-6u)/units,(height-8u)/7u);
  if(!scale)return {};
  std::vector<uint8_t> pixels(size_t(width)*height*4u);
  auto pixel=[&](uint32_t x,uint32_t y,std::array<uint8_t,4> rgba){
    const auto i=(size_t(y)*width+x)*4u;
    std::copy(rgba.begin(),rgba.end(),pixels.begin()+static_cast<ptrdiff_t>(i));
  };
  const uint32_t left=1,top=height/8u,right=width-2u,bottom=height-1u-top;
  for(uint32_t y=top;y<=bottom;++y)for(uint32_t x=left;x<=right;++x){
    if((x==left||x==right)&&(y==top||y==bottom))continue;
    pixel(x,y,(x==left||x==right||y==top||y==bottom)?
          std::array<uint8_t,4>{208,218,195,255}:
          std::array<uint8_t,4>{19,28,22,255});
  }
  const auto x0=(width-units*scale)/2u,y0=(height-7u*scale)/2u;
  for(uint32_t i=0;i<letters;++i){
    const char c=label[i];
    const auto index=static_cast<size_t>(c>='A'&&c<='Z'?c-'A':26+c-'0');
    if(index>=kKeyFont.size())return {};
    for(uint32_t y=0;y<7;++y)for(uint32_t x=0;x<5;++x)
      if(kKeyFont[index][y]&(1u<<(4u-x)))
        for(uint32_t dy=0;dy<scale;++dy)for(uint32_t dx=0;dx<scale;++dx)
          pixel(x0+(i*6u+x)*scale+dx,y0+y*scale+dy,{245,248,236,255});
  }
  return pixels;
}
// Use the retail PC image, aspect-preserving, in the exact original
// live Xbox glyph extent. No non-uniform stretching of keyboard artwork.
inline std::vector<uint8_t> FitPcPromptImage(const std::vector<uint8_t>& src,
    uint32_t sw,uint32_t sh,uint32_t w,uint32_t h) {
  if(!sw || !sh || sw>4096 || sh>4096 || !w || !h || w>256 || h>256 ||
     src.size()!=size_t(sw)*sh*4u)return {};
  auto tw=w,th=h;
  if(uint64_t(w)*sh<=uint64_t(h)*sw)th=std::max(1u,uint32_t(uint64_t(w)*sh/sw));
  else tw=std::max(1u,uint32_t(uint64_t(h)*sw/sh));
  std::vector<uint8_t> result(size_t(w)*h*4u);
  const auto ox=(w-tw)/2u,oy=(h-th)/2u;
  for(uint32_t y=0;y<th;++y)for(uint32_t x=0;x<tw;++x){
    const auto from=(size_t(uint64_t(y)*sh/th)*sw+uint64_t(x)*sw/tw)*4u;
    const auto to=(size_t(y+oy)*w+x+ox)*4u;
    std::copy_n(src.data()+from,4,result.data()+to);
  }
  return result;
}
} // namespace hulk::eor::ui
