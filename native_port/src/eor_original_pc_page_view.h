#pragma once
// Native multi-row views reuse the seven original MenuItemTxt label slots.
// Preserve the localized original text and value-widget visibility; never
// mutate a new guest owner which happens to reuse an old object address.
#include "eor_original_widget_api.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace hulk::eor::ui {
inline constexpr size_t kNativePcPageRows=7;
struct PcMenuVisual {
  std::string title,compact;
  std::array<std::string,kNativePcPageRows> rows{};
  size_t count=0;
};
template<class Paint>
bool PaintPcMenuVisual(Paint& paint,const PcMenuVisual& view) {
  if constexpr(requires {paint(view);})return paint(view);
  else return paint(std::string_view(view.compact));
}
template<class Paint>
bool PaintPcMenuEntry(Paint& paint,std::string_view label) {
  PcMenuVisual view;view.title=label;view.compact=label;
  return PaintPcMenuVisual(paint,view);
}
inline constexpr uint32_t kOriginalPcTextVtable=0x82078A58u;
inline constexpr uint32_t kOriginalPcTextIntVtable=0x8207A1D8u;
inline constexpr uint32_t kOriginalPcTextVariableIntVtable=0x8207A8B0u;
template<class Read>
bool VerifyOriginalPcPageTextAbi(Read& read) {
  constexpr std::array<WidgetCodePin,3> pins{{
    {0x8271AA60u,244,0x3262350a84cd4fcaull}, // original text Hidden setter
    {0x826C0480u,56,0x1caf5ac58d78cbb7ull}, // original tagged UTF-16 string reader
    {0x8271D340u,208,0x1aa6dad8d2272d14ull}, // original text setter
  }};
  std::array<uint8_t,244> bytes{};
  for(const auto& pin:pins) {
    if(!read(pin.address,bytes.data(),pin.length))return false;
    uint64_t h=0xcbf29ce484222325ull;
    for(size_t i=0;i<pin.length;++i)h=(h^bytes[i])*0x100000001b3ull;
    if(h!=pin.fnv)return false;
  }
  for(auto table:{kOriginalPcTextVtable,kOriginalPcTextIntVtable,kOriginalPcTextVariableIntVtable})
    if(WidgetWord(table,184,read)!=0x825B87E8u ||
       WidgetWord(table,188,read)!=0x825B8820u ||
       WidgetWord(table,412,read)!=0x8271F8A0u)return false;
  return true;
}
struct OriginalPcTextSlot {
  OriginalWidgetIdentity id{};
  uint32_t resource=0,vtable=0;
  bool hidden=false;
  std::vector<uint16_t> text;
};
inline bool IsOriginalPcTextType(uint32_t definition,uint32_t vtable) noexcept {
  return (definition==kTextTemplate && vtable==kOriginalPcTextVtable) ||
      (definition==WidgetSymbol("UIWidgetTextIntBruce") && vtable==kOriginalPcTextIntVtable) ||
      (definition==WidgetSymbol("UIWidgetTextVariableIntBruce") && vtable==kOriginalPcTextVariableIntVtable);
}
template<class Read>
std::optional<bool> ReadOriginalPcTextHidden(uint32_t object,Read& read) {
  if(!object || object>UINT32_MAX-176u)return {};
  uint8_t b=0;
  if(!read(object+176u,&b,1) || b>1u)return {};
  return b!=0;
}
template<class Read>
std::optional<std::vector<uint16_t>> ReadOriginalPcText(uint32_t object,Read& read) {
  // 8271D340 uses the string at this+496; 826C0480 proves the tag rule.
  if(WidgetWord(object,0,read)!=kOriginalPcTextVtable)return {};
  const auto tagged=WidgetWord(object,496,read);
  if(!tagged)return {};
  std::vector<uint16_t> text;
  if(!*tagged)return text;
  uint32_t p=*tagged&~1u;
  if(!(*tagged&1u)) {
    const auto ptr=WidgetWord(*tagged,0,read);
    if(!ptr || !*ptr)return {};
    p=*ptr;
  }
  if(!p || (p&1u))return {};
  for(size_t i=0;i<=255u;++i) {
    if(uint64_t(p)+2u*i>UINT32_MAX-1u)return {};
    std::array<uint8_t,2> b{};
    if(!read(p+uint32_t(2*i),b.data(),b.size()))return {};
    const auto unit=uint16_t(uint16_t(b[0])<<8|b[1]);
    if(!unit)return text;
    if(i==255u)return {};
    text.push_back(unit);
  }
  return {};
}
template<class Read>
std::optional<OriginalPcTextSlot> CaptureOriginalPcTextSlot(
    const OriginalWidgetIdentity& id,bool label,Read& read) {
  const auto vtable=WidgetWord(id.object,0,read),resource=WidgetWord(id.object,124,read);
  const auto hidden=ReadOriginalPcTextHidden(id.object,read);
  if(!resource || !*resource || !vtable || !hidden ||
     !IsOriginalPcTextType(id.definition,*vtable) ||
     (label && id.definition!=kTextTemplate))return {};
  OriginalPcTextSlot out{id,*resource,*vtable,*hidden,{}};
  if(label) {
    auto text=ReadOriginalPcText(id.object,read);
    if(!text)return {};
    out.text=std::move(*text);
  }
  return out;
}
template<class Read>
bool OriginalPcTextSlotStillOwned(const OriginalPcTextSlot& slot,Read& read) {
  const auto id=ReadOriginalWidget(slot.id.object,read);
  return id && id->parent==slot.id.parent && id->symbol==slot.id.symbol &&
      id->definition==slot.id.definition && WidgetWord(id->object,124,read)==slot.resource &&
      WidgetWord(id->object,0,read)==slot.vtable;
}
class OriginalPcNativePageView {
 public:
  bool Active() const noexcept {return owner_!=0;}
  void Discard() noexcept {owner_=resource_=0;labels_={};values_={};}
  template<class Read,class Paint,class Hide>
  bool Restore(uint32_t owner,Read& read,Paint& paint,Hide& hide) {
    if(!Active())return true;
    if(!VerifyOriginalPcPageTextAbi(read))return false;
    if(owner_!=owner || WidgetWord(owner_,124,read)!=resource_)return false;
    bool ok=true;
    // Restore only still-owned widgets. A partially replaced page must not
    // authorize writes to the new objects.
    for(size_t i=0;i<kNativePcPageRows;++i) {
      const auto& l=labels_[i];const auto& v=values_[i];
      if(OriginalPcTextSlotStillOwned(l,read)) {
        if(!paint(l.id.object,l.text))ok=false;
        if(!hide(l.id.object,l.hidden))ok=false;
      } else ok=false;
      if(OriginalPcTextSlotStillOwned(v,read)) {
        if(!hide(v.id.object,v.hidden))ok=false;
      } else ok=false;
    }
    if(ok)Discard();
    return ok;
  }
  template<class Read,class Find,class Paint,class Hide>
  bool Show(uint32_t owner,const PcMenuVisual& frame,Read& read,Find& find,Paint& paint,Hide& hide) {
    if(!frame.count || frame.count>kNativePcPageRows || !VerifyOriginalPcPageTextAbi(read))return false;
    if(!Active() && !Capture(owner,read,find))return false;
    if(owner_!=owner || WidgetWord(owner_,124,read)!=resource_)return false;
    for(size_t i=0;i<kNativePcPageRows;++i)
      if(!OriginalPcTextSlotStillOwned(labels_[i],read) || !OriginalPcTextSlotStillOwned(values_[i],read))return false;
    // Prepare all strings before changing any native object.
    std::array<std::vector<uint16_t>,kNativePcPageRows> text;
    for(size_t i=0;i<frame.count;++i) {
      if(frame.rows[i].empty() || frame.rows[i].size()>255u)return false;
      for(unsigned char c:frame.rows[i]) {
        if(c<32u || c>126u)return false;
        text[i].push_back(c);
      }
    }
    try {
      for(size_t i=0;i<kNativePcPageRows;++i) {
        if(!hide(values_[i].id.object,true) ||
           (i<frame.count && !paint(labels_[i].id.object,text[i])) ||
           !hide(labels_[i].id.object,i>=frame.count)) {
          (void)Restore(owner,read,paint,hide);return false;
        }
      }
    } catch(...) {(void)Restore(owner,read,paint,hide);return false;}
    return true;
  }
 private:
  template<class Read,class Find>
  bool Capture(uint32_t owner,Read& read,Find& find) {
    const auto root=ReadOriginalWidget(owner,read);
    if(!root || (root->definition!=kSettingsDefinition && root->definition!=kFrontendSettingsDefinition))return false;
    const auto resource=WidgetWord(owner,124,read);
    if(!resource || !*resource)return false;
    const auto game=find(owner,WidgetSymbol("gameOptions"));
    const auto audio=find(owner,WidgetSymbol("audioOptions"));
    if(!game || game->definition!=kGameSettingsDefinition || !audio || audio->definition!=0xDD663BE3u)return false;
    constexpr std::array<std::string_view,kNativePcPageRows> label_names{
      "InvXLookTxt","InvYLookTxt","VibrationTxt","MusicVolTxt","SFXVolTxt","DialogVolTxt","SubtitlesTxt"};
    constexpr std::array<std::string_view,kNativePcPageRows> value_names{
      "option1","option2","option3","option1","option2","option3","option4"};
    std::array<OriginalPcTextSlot,kNativePcPageRows> labels,values;
    std::array<uint32_t,2*kNativePcPageRows> objects{};
    for(size_t i=0;i<kNativePcPageRows;++i) {
      const auto group=i<3u?game->object:audio->object;
      const auto row=find(group,WidgetSymbol(label_names[i]));
      const auto value=find(group,WidgetSymbol(value_names[i]));
      if(!row || row->definition!=kMenuItemTemplate || !value)return false;
      const auto label=find(row->object,kTextChild);
      if(!label)return false;
      auto l=CaptureOriginalPcTextSlot(*label,true,read);
      auto v=CaptureOriginalPcTextSlot(*value,false,read);
      if(!l || !v)return false;
      objects[2*i]=l->id.object;objects[2*i+1]=v->id.object;
      labels[i]=std::move(*l);values[i]=std::move(*v);
    }
    std::sort(objects.begin(),objects.end());
    if(std::adjacent_find(objects.begin(),objects.end())!=objects.end())return false;
    labels_=std::move(labels);values_=std::move(values);owner_=owner;resource_=*resource;
    return true;
  }
  uint32_t owner_=0,resource_=0;
  std::array<OriginalPcTextSlot,kNativePcPageRows> labels_{},values_{};
};
} // namespace hulk::eor::ui
