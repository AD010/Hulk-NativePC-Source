#pragma once
// PC-only settings in an existing genuine EoR MenuItemTxt. The original
// console storage row supplies layout, focus and native text rendering.
#include "eor_original_widget_api.h"
#include "native_pc_menu_rows.h"
#include <charconv>
#include <map>
#include <string>
#include <vector>

namespace hulk::eor::ui {
enum class PcEvent : uint8_t { None, Enter, Select, Up, Down, Increase, Decrease, Back };
inline constexpr PcEvent OriginalSettingsEvent(uint32_t symbol) noexcept {
  // Exact method footer identifiers in both pinned USA settings objects.
  // StateEnter/Back use explicit event aliases, NOT the method-name CRC.
  switch(symbol) {
    case 0x655fc260u:return PcEvent::Enter;
    case 0x7b526ef1u:return PcEvent::Select;
    case 0x4f09f4afu:return PcEvent::Up;
    case 0x37340621u:return PcEvent::Down;
    case 0x37aeaa65u:return PcEvent::Increase;
    case 0x8498e384u:return PcEvent::Decrease;
    case 0xc409cb6au:return PcEvent::Back;
    default:return PcEvent::None;
  }
}
template<class Read>
bool VerifySettingsEventAbi(Read& read) {
  constexpr std::array<WidgetCodePin,4> pins{{
    {0x825BAAC8u,88u,0x25d234d049509886ull}, // original event invocation
    {0x825B9528u,176u,0x11c8c5c8f82bb5a5ull}, // exact method lookup
    {0x825B9D98u,92u,0x475c293b6a91ac08ull}, // original bool field getter
    {0x8271C218u,56u,0x1e5dbabf904362c6ull}, // Hidden field read
  }};
  std::array<uint8_t,176> bytes{};
  for(const auto& pin:pins) {
    if(!read(pin.address,bytes.data(),pin.length))return false;
    uint64_t h=0xcbf29ce484222325ull;
    for(size_t i=0;i<pin.length;++i)h=(h^bytes[i])*0x100000001b3ull;
    if(h!=pin.fnv)return false;
  }
  return true;
}
struct PcSettingsEvent { uint32_t root=0, context=0, definition=0; PcEvent event=PcEvent::None; };
template<class Read>
std::optional<PcSettingsEvent> CapturePcSettingsEvent(uint32_t root,uint32_t context,Read& read) {
  const auto symbol=WidgetWord(context,0,read);
  if(!symbol)return {};
  const auto event=OriginalSettingsEvent(*symbol);
  if(event==PcEvent::None)return {};
  const auto id=ReadOriginalWidget(root,read);
  if(!id || (id->definition!=kSettingsDefinition && id->definition!=kFrontendSettingsDefinition))return {};
  const auto resource=WidgetWord(root,124,read);
  const auto def=resource?WidgetWord(*resource,8,read):std::nullopt;
  const auto tagged=def?WidgetWord(*def,4,read):std::nullopt;
  if(!tagged || !*tagged)return {};
  uint32_t name=*tagged;
  if(!(name&2u)) {
    if(name&1u)name&=~1u;
    else {const auto ptr=WidgetWord(name,0,read);if(!ptr)return {};name=*ptr;}
  }
  const std::string_view expected=id->definition==kSettingsDefinition?
      "uipage_pause_settings":"uiwidget_frontend_settings";
  std::array<uint8_t,32> bytes{};
  if(!name || uint64_t(name)+expected.size()>UINT32_MAX ||
     !read(name,bytes.data(),expected.size()+1u) || bytes[expected.size()])return {};
  for(size_t i=0;i<expected.size();++i) {
    auto ch=bytes[i];if(ch>='A' && ch<='Z')ch=uint8_t(ch+32u);
    if(ch!=uint8_t(expected[i]))return {};
  }
  if(!VerifySettingsEventAbi(read))return {};
  return PcSettingsEvent{root,context,id->definition,event};
}
inline constexpr std::string_view kPcEntryText="PC Graphics / Controls";
// Do not rebind the keys driving this same native menu while it is open.
// Gameplay bindings, camera, display and graphics are editable here.
inline bool PcStorageRowOption(const options::PcMenuRow& row,bool controls) noexcept {
  using S=options::PcMenuSection;using K=options::PcMenuRowKind;
  if(controls) {
    if(row.section!=S::Controls)return false;
    const std::string_view key=row.key;
    return key!="hulk_bind_confirm" && key!="hulk_bind_back" &&
        key!="hulk_bind_alt_back" && key!="hulk_bind_start" &&
        key!="hulk_bind_select" && !key.starts_with("hulk_bind_menu_");
  }
  return (row.section==S::Display || row.section==S::Graphics ||
          row.section==S::PostProcessing || row.section==S::Enhancements) &&
         (row.kind==K::Choice || row.kind==K::Number);
}
inline std::vector<std::string_view> PcStorageChoices(const options::PcMenuRow& row) {
  using K=options::PcMenuRowKind;
  if(row.kind==K::Binding)return {options::kBindingTokens.begin(),options::kBindingTokens.end()};
  if(row.kind==K::Number) {
    if(std::string_view(row.key)=="native_x_renderer_sharpness")
      return {"0.0","0.1","0.2","0.3","0.4","0.5"};
    return {"0.25","0.5","0.75","1.0","1.25","1.5","2.0","3.0","4.0"};
  }
  std::vector<std::string_view> values;
  auto rest=row.choices;
  while(!rest.empty()) {
    const auto end=rest.find('|');values.push_back(rest.substr(0,end));
    if(end==std::string_view::npos)break;
    rest.remove_prefix(end+1);
  }
  return values;
}
inline bool PcStorageSameValue(std::string_view a,std::string_view b) noexcept {
  if(a==b || options::KeyNameEquals(a,b))return true;
  double x=0,y=0;
  const auto p=std::from_chars(a.data(),a.data()+a.size(),x);
  const auto q=std::from_chars(b.data(),b.data()+b.size(),y);
  return p.ec==std::errc{} && p.ptr==a.data()+a.size() &&
         q.ec==std::errc{} && q.ptr==b.data()+b.size() && x==y;
}
inline std::string_view PcStorageNextValue(const options::PcMenuRow& row,
    std::string_view current,bool forward) {
  auto values=PcStorageChoices(row);
  if(values.empty())return {};
  if(row.kind==options::PcMenuRowKind::Binding) {
    options::AnyBindingKey(current,[&](std::string_view token){current=token;return true;});
  }
  for(size_t i=0;i<values.size();++i)if(PcStorageSameValue(current,values[i]))
    return values[(i+(forward?1u:values.size()-1u))%values.size()];
  return forward?values.front():values.back();
}
// Session is called ONLY on the original owning game/VM thread, with
// freshly authenticated native row visibility and selector focus.
class OriginalPcStorageMenu {
 public:
  void Leave() noexcept {owner_=row_=0;mode_=0;cursor_=0;}
  bool Owns(uint32_t owner,uint32_t row) const noexcept {
    return owner_ && row_ && owner_==owner && row_==row;
  }
  template<class Paint>
  bool Enter(uint32_t owner,uint32_t row,Paint&& paint) {
    Leave();if(!owner || !row || !paint(kPcEntryText))return false;
    owner_=owner;row_=row;return true;
  }
  template<class Paint,class ReadValue,class Commit>
  bool Handle(uint32_t owner,uint32_t row,PcEvent event,bool focused,
      Paint&& paint,ReadValue&& read_value,Commit&& commit) {
    if(!Owns(owner,row))return false;
    if(!focused) {if(mode_) {mode_=0;cursor_=0;(void)paint(kPcEntryText);}return false;}
    if(!mode_) {
      if(event!=PcEvent::Select)return false;
      mode_=1;cursor_=0;
    } else if(event==PcEvent::Back) {
      if(!paint(kPcEntryText))return false;
      mode_=0;cursor_=0;return true;
    } else if(event==PcEvent::Select) {
      mode_=mode_==1?2:1;cursor_=0;
    } else if(event!=PcEvent::Up && event!=PcEvent::Down &&
              event!=PcEvent::Increase && event!=PcEvent::Decrease)return false;
    std::vector<const options::PcMenuRow*> rows;
    for(const auto& r:options::kPcMenuRows)if(PcStorageRowOption(r,mode_==2))rows.push_back(&r);
    if(rows.empty()){Leave();return false;}
    cursor_%=rows.size();
    if(event==PcEvent::Up)cursor_=(cursor_+rows.size()-1)%rows.size();
    if(event==PcEvent::Down)cursor_=(cursor_+1)%rows.size();
    const auto& selected=*rows[cursor_];
    const auto pending=pending_.find(selected.key);
    std::string value=pending==pending_.end()?read_value(selected.key):pending->second;
    bool failed=false,committed=false;
    if(event==PcEvent::Increase || event==PcEvent::Decrease) {
      const auto next=PcStorageNextValue(selected,value,event==PcEvent::Increase);
      if(!next.empty() && options::IsPcMenuChoice(selected,next)) {
        const auto result=commit(selected.key,next);
        failed=!result.persisted;committed=result.persisted;
        if(result.persisted) {
          value=std::string(next);
          if(result.live_applied)pending_.erase(selected.key);
          else pending_[selected.key]=value;
        }
      }
    }
    std::string text=mode_==1?"Graphics ":"Controls ";
    text+=std::to_string(cursor_+1)+"/"+std::to_string(rows.size())+": ";
    text+=selected.label;text+=" = ";text+=value;
    if(failed)text+=" (save failed)";
    else if(pending_.contains(selected.key))text+=" (restart)";
    if(paint(text))return true;
    // Never let the original action run after a persistent PC change.
    // A vanished widget ends the session before another event can apply.
    Leave();return committed;
  }
 private:
  uint32_t owner_=0,row_=0;unsigned mode_=0;size_t cursor_=0;
  std::map<std::string,std::string,std::less<>> pending_;
};
} // namespace hulk::eor::ui
