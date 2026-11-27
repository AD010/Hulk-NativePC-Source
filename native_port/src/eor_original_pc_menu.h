#pragma once
// PC-only settings in an existing genuine EoR MenuItemTxt. The original
// console storage row supplies layout, focus and native text rendering.
#include "eor_original_pc_page_view.h"
#include "native_pc_menu_rows.h"
#include <charconv>
#include <cmath>
#include <functional>
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
template<class Read>
bool VerifyDirectSettingsMethodAbi(Read& read) {
  // 825BAC08 resolves the same original method as 825BAAC8, but returns
  // the reference-counted VM result, NOT the event-dispatch boolean.
  std::array<uint8_t,72> bytes{};
  if(!read(0x825BAC08u,bytes.data(),bytes.size()))return false;
  uint64_t h=0xcbf29ce484222325ull;
  for(uint8_t b:bytes)h=(h^b)*0x100000001b3ull;
  return h==0x6c1f3b011d2dd995ull;
}
inline constexpr bool OriginalSettingsEntrySucceeded(bool direct_method,uint32_t result) noexcept {
  return direct_method ? result!=0u : result==1u;
}
struct PcSettingsEvent { uint32_t root=0, context=0, definition=0; PcEvent event=PcEvent::None; };
template<class Read>
std::optional<PcSettingsEvent> CapturePcSettingsEvent(uint32_t root,uint32_t context,Read& read) {
  // 825BAAC8 refuses a disabled owner (PPC bit 2).
  const auto flags=WidgetWord(root,16,read);
  if(!flags || (*flags&0x20000000u) || !WidgetWord(context,20,read))return {};
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
inline constexpr std::string_view kPcEntryText="PC Graphics / Controls / Audio / Exit";
// Do not rebind the keys driving this same native menu while it is open.
// Gameplay bindings, camera, display and graphics are editable here.
enum class PcSettingsSection { Entry, Graphics, Controls, Audio, Exit };
inline bool PcStorageRowOption(const options::PcMenuRow& row,PcSettingsSection section) noexcept {
  using S=options::PcMenuSection;using K=options::PcMenuRowKind;
  if(section==PcSettingsSection::Audio)return row.section==S::Audio && row.kind==K::Choice;
  if(section==PcSettingsSection::Controls) {
    if(row.section!=S::Controls)return false;
    const std::string_view key=row.key;
    return key!="hulk_bind_confirm" && key!="hulk_bind_back" &&
        key!="hulk_bind_alt_back" && key!="hulk_bind_start" &&
        key!="hulk_bind_select" && !key.starts_with("hulk_bind_menu_");
  }
  return section==PcSettingsSection::Graphics && (row.section==S::Display || row.section==S::Graphics ||
          row.section==S::PostProcessing || row.section==S::Enhancements) &&
         (row.kind==K::Choice || row.kind==K::Number || row.kind==K::WindowSize);
}
inline std::vector<std::string_view> PcStorageChoices(const options::PcMenuRow& row) {
  using K=options::PcMenuRowKind;
  if(row.kind==K::Binding)return {options::kBindingTokens.begin(),options::kBindingTokens.end()};
  if(row.kind==K::WindowSize) {
    std::vector<std::string_view> sizes;
    for(const auto& preset:display::kWindowPresets)sizes.push_back(preset.Dimensions());
    return sizes;
  }
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
  if(a.empty() || b.empty())return false;
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
  // A valid custom numeric value can lie between two menu presets.
  // Step in the requested direction rather than jumping to the opposite end.
  if(row.kind==options::PcMenuRowKind::Number && !current.empty()) {
    double number=0;
    const auto parsed=std::from_chars(current.data(),current.data()+current.size(),number);
    if(parsed.ec==std::errc{} && parsed.ptr==current.data()+current.size() && std::isfinite(number)) {
      std::string_view nearest;
      double nearest_value=0;
      for(const auto value:values) {
        double candidate=0;
        const auto p=std::from_chars(value.data(),value.data()+value.size(),candidate);
        if(p.ec!=std::errc{} || p.ptr!=value.data()+value.size() || !std::isfinite(candidate))continue;
        if(forward ? candidate<=number : candidate>=number)continue;
        if(nearest.empty() || (forward ? candidate<nearest_value : candidate>nearest_value)) {
          nearest=value;nearest_value=candidate;
        }
      }
      if(!nearest.empty())return nearest;
      return forward?values.back():values.front();
    }
  }
  return forward?values.front():values.back();
}
// Session is called ONLY on the original owning game/VM thread, with
// freshly authenticated native row visibility and selector focus.
class OriginalPcStorageMenu {
 public:
  void Leave() noexcept {owner_=row_=0;section_=PcSettingsSection::Entry;cursor_=0;}
  bool Owns(uint32_t owner,uint32_t row) const noexcept {
    return owner_ && row_ && owner_==owner && row_==row;
  }
  template<class Paint>
  bool Enter(uint32_t owner,uint32_t row,Paint&& paint) {
    Leave();if(!owner || !row || !PaintPcMenuEntry(paint,kPcEntryText))return false;
    owner_=owner;row_=row;return true;
  }
  template<class Paint,class ReadValue,class Commit>
  bool Handle(uint32_t owner,uint32_t row,PcEvent event,bool focused,
      Paint&& paint,ReadValue&& read_value,Commit&& commit,
      const std::function<bool()>& request_exit={}) {
    if(!Owns(owner,row))return false;
    if(!focused) {if(section_!=PcSettingsSection::Entry) {section_=PcSettingsSection::Entry;cursor_=0;(void)PaintPcMenuEntry(paint,kPcEntryText);}return false;}
    if(section_==PcSettingsSection::Entry) {
      if(event!=PcEvent::Select)return false;
      section_=PcSettingsSection::Graphics;cursor_=0;
    } else if(event==PcEvent::Back) {
      if(!PaintPcMenuEntry(paint,kPcEntryText))return false;
      section_=PcSettingsSection::Entry;cursor_=0;return true;
    } else if(section_==PcSettingsSection::Exit) {
      if(event!=PcEvent::Select)return true;
      if(request_exit && request_exit()){Leave();return true;}
      (void)PaintPcMenuEntry(paint,"Exit unavailable - Back to cancel");
      return true;
    } else if(event==PcEvent::Select) {
      switch(section_) {
        case PcSettingsSection::Graphics:section_=PcSettingsSection::Controls;break;
        case PcSettingsSection::Controls:section_=PcSettingsSection::Audio;break;
        case PcSettingsSection::Audio:
          // This is a real, focused EoR selector event. Require another
          // explicit Select to confirm; Back cancels via the normal path.
          if(request_exit) {
            if(!PaintPcMenuEntry(paint,"Exit to Windows? Select to confirm / Back to cancel"))return true;
            section_=PcSettingsSection::Exit;cursor_=0;return true;
          }
          section_=PcSettingsSection::Graphics;break;
        default:section_=PcSettingsSection::Graphics;break;
      }
      cursor_=0;
    } else if(event!=PcEvent::Up && event!=PcEvent::Down &&
              event!=PcEvent::Increase && event!=PcEvent::Decrease)return false;
    std::vector<const options::PcMenuRow*> rows;
    for(const auto& r:options::kPcMenuRows)if(PcStorageRowOption(r,section_))rows.push_back(&r);
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
    const char* title=section_==PcSettingsSection::Graphics?"Graphics ":
        section_==PcSettingsSection::Controls?"Controls ":"Audio ";
    std::string text=title;
    text+=std::to_string(cursor_+1)+"/"+std::to_string(rows.size())+": ";
    text+=selected.label;text+=" = ";text+=value;
    if(failed)text+=" (save failed)";
    else if(pending_.contains(selected.key))text+=" (restart)";
    PcMenuVisual frame;frame.compact=text;
    const size_t start=(cursor_/kNativePcPageRows)*kNativePcPageRows;
    frame.count=std::min(kNativePcPageRows,rows.size()-start);
    frame.title=title;
    frame.title+=std::to_string(start/kNativePcPageRows+1)+"/"+
        std::to_string((rows.size()+kNativePcPageRows-1)/kNativePcPageRows);
    for(size_t i=0;i<frame.count;++i) {
      const auto& r=*rows[start+i];
      const auto p=pending_.find(r.key);
      const bool active=start+i==cursor_;
      frame.rows[i]=active?"> ":"  ";
      frame.rows[i]+=r.label;frame.rows[i]+=" = ";
      frame.rows[i]+=active?value:(p==pending_.end()?read_value(r.key):p->second);
      if(active && failed)frame.rows[i]+=" (save failed)";
      else if(p!=pending_.end())frame.rows[i]+=" (restart)";
    }
    if(PaintPcMenuVisual(paint,frame))return true;
    // Never let the original action run after a persistent PC change.
    // A vanished widget ends the session before another event can apply.
    Leave();return committed;
  }
 private:
  uint32_t owner_=0,row_=0;PcSettingsSection section_=PcSettingsSection::Entry;size_t cursor_=0;
  std::map<std::string,std::string,std::less<>> pending_;
};
} // namespace hulk::eor::ui
