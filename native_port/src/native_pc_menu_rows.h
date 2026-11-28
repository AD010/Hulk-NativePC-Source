#pragma once
// PC-specific rows for the genuine EoR frontend, independent of the
// host drawing library. All actions are backed by the existing validated
// option store. The original widget adapter must supply the current owner,
// input events, and actual renderer; this file never invents a guest ABI.
#include "native_pc_menu_actions.h"
#include <array>
#include <cstdint>
#include <string_view>
namespace hulk::options {
enum class PcMenuSection : uint8_t {Display,Graphics,PostProcessing,Audio,Controls,Enhancements,Developer};
enum class PcMenuRowKind : uint8_t {Choice,Number,Binding,WindowSize,Exit};
struct PcMenuRow {
  const char* label;
  const char* key;
  PcMenuSection section;
  PcMenuRowKind kind;
  std::string_view choices; // delimiter-separated internal values, not localization IDs
};
inline constexpr PcMenuRow kPcMenuRows[] = {
  {"Fullscreen","fullscreen",PcMenuSection::Display,PcMenuRowKind::Choice,"false|true"},
  {"Window resolution","window_size",PcMenuSection::Display,PcMenuRowKind::WindowSize,""},
  {"Frame rate limit","hulk_guest_swap_fps_cap",PcMenuSection::Display,PcMenuRowKind::Choice,"30|60|90|120|144|0"},
  {"60 Hz Havok catch-up","hulk_original_havok_catchup",PcMenuSection::Enhancements,PcMenuRowKind::Choice,"false|true"},
  {"Vertical synchronization","vsync",PcMenuSection::Display,PcMenuRowKind::Choice,"false|true"},
  {"Rendering scale","native_x_renderer_resolution_scale",PcMenuSection::Graphics,PcMenuRowKind::Choice,"0.0|1.0|2.0"},
  {"Post antialiasing","native_x_renderer_antialiasing",PcMenuSection::PostProcessing,PcMenuRowKind::Choice,"off|fxaa|smaa"},
  {"Presentation filter","native_x_renderer_scaling",PcMenuSection::PostProcessing,PcMenuRowKind::Choice,"bilinear|fsr|nis"},
  {"Sharpening","native_x_renderer_sharpness",PcMenuSection::PostProcessing,PcMenuRowKind::Number,""},
  {"Mute output","audio_mute",PcMenuSection::Audio,PcMenuRowKind::Choice,"false|true"},
  {"Music volume","hulk_original_music_volume",PcMenuSection::Audio,PcMenuRowKind::Choice,"-1|0|10|20|30|40|50|60|70|80|90|100"},
  {"Effects volume","hulk_original_sfx_volume",PcMenuSection::Audio,PcMenuRowKind::Choice,"-1|0|10|20|30|40|50|60|70|80|90|100"},
  {"Dialogue volume","hulk_original_dialog_volume",PcMenuSection::Audio,PcMenuRowKind::Choice,"-1|0|10|20|30|40|50|60|70|80|90|100"},
  {"Audio worker priority","audio_thread_priority",PcMenuSection::Audio,PcMenuRowKind::Choice,"false|true"},
  {"Exclusive audio output","audio_wasapi_exclusive",PcMenuSection::Audio,PcMenuRowKind::Choice,"false|true"},
  {"Audio buffer","audio_maxqframes",PcMenuSection::Audio,PcMenuRowKind::Choice,"4|8|12|16|32|64"},
  {"Audio callback spacing","audio_callback_min_interval_us",PcMenuSection::Audio,PcMenuRowKind::Choice,"0|3000|4000"},
  {"Invert mouse X","hulk_mouse_invert_x",PcMenuSection::Controls,PcMenuRowKind::Choice,"false|true"},
  {"Invert mouse Y","hulk_mouse_invert_y",PcMenuSection::Controls,PcMenuRowKind::Choice,"false|true"},
  {"Mouse horizontal speed","hulk_mouse_horizontal_multiplier",PcMenuSection::Controls,PcMenuRowKind::Number,""},
  {"Mouse vertical speed","hulk_mouse_vertical_multiplier",PcMenuSection::Controls,PcMenuRowKind::Number,""},
  {"Move forward","hulk_bind_forward",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Move backward","hulk_bind_backward",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Move left","hulk_bind_left",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Move right","hulk_bind_right",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Jump / Confirm","hulk_bind_confirm",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Attack","hulk_bind_primary",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Secondary attack","hulk_bind_secondary",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Back / Cancel","hulk_bind_back",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Alternate back","hulk_bind_alt_back",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Left ability","hulk_bind_lb",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Right ability","hulk_bind_rb",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Left trigger","hulk_bind_lt",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Right trigger","hulk_bind_rt",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Pause","hulk_bind_start",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Walk","hulk_bind_walk",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Menu up","hulk_bind_menu_up",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Menu down","hulk_bind_menu_down",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Menu left","hulk_bind_menu_left",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Menu right","hulk_bind_menu_right",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Select / Back button","hulk_bind_select",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Right stick click","hulk_bind_rstick_click",PcMenuSection::Controls,PcMenuRowKind::Binding,""},
  {"Anisotropic filtering","anisotropic_override",PcMenuSection::Enhancements,PcMenuRowKind::Choice,"-1|0|1|2|3|4|5"},
  {"Direct EoR Vulkan","hulk_eor_vulkan_replay",PcMenuSection::Developer,PcMenuRowKind::Choice,"false|true"},
  {"Capture original draw","hulk_eor_xdk_capture",PcMenuSection::Developer,PcMenuRowKind::Choice,"false|true"},
  {"Capture source geometry","hulk_eor_xdk_source_mesh_capture",PcMenuSection::Developer,PcMenuRowKind::Choice,"false|true"},
  {"Exit to Windows","exit",PcMenuSection::Display,PcMenuRowKind::Exit,""}
};
inline constexpr const PcMenuRow* FindPcMenuRow(std::string_view key) noexcept {
  for(const auto& row:kPcMenuRows)if(key==row.key)return &row;
  return nullptr;
}
inline bool IsPcMenuChoice(const PcMenuRow& row,std::string_view value) noexcept {
  if(row.kind==PcMenuRowKind::WindowSize)
    return std::any_of(display::kWindowPresets.begin(),display::kWindowPresets.end(),
                       [&](const auto& preset){return preset.Dimensions()==value;});
  if(row.kind==PcMenuRowKind::Binding)return IsBindingToken(value);
  if(row.kind==PcMenuRowKind::Number)return !value.empty(); // store verifies finite bounds
  if(row.kind!=PcMenuRowKind::Choice || value.empty())return false;
  auto rest=row.choices;
  while(!rest.empty()) {
    const auto end=rest.find('|');
    if(rest.substr(0,end)==value)return true;
    if(end==std::string_view::npos)break;
    rest.remove_prefix(end+1);
  }
  return false;
}
// Each page lifetime has a different generation even if the guest allocator
// reuses an address. No delayed event may apply settings after leaving a page.
class PcMenuPageSession {
 public:
  void Enter(uint32_t owner,uint64_t generation) noexcept {
    owner_=owner; generation_=generation; exit_pending_=false;
  }
  void Leave() noexcept {owner_=0;generation_=0;exit_pending_=false;}
  bool Owns(uint32_t owner,uint64_t generation) const noexcept {
    return owner_ && generation_ && owner==owner_ && generation==generation_;
  }
  template<class Apply>
  PcMenuCommitResult Select(uint32_t owner,uint64_t generation,
      std::string_view key,std::string_view value,Apply&& apply) {
    if(!Owns(owner,generation) || exit_pending_)return {};
    const auto* row=FindPcMenuRow(key);
    if(!row || !IsPcMenuChoice(*row,value))return {};
    // Geometry capture and its parent capture are one persistent transaction.
    if(key=="hulk_eor_xdk_source_mesh_capture")
      return {SaveLiveSourceMeshCapture(value=="true"),false};
    return CommitLivePcMenuValue(key,value,apply);
  }
  bool Resolution(uint32_t owner,uint64_t generation,uint32_t w,uint32_t h) {
    return Owns(owner,generation) && !exit_pending_ && SaveLiveWindowSize(w,h);
  }
  bool RequestExit(uint32_t owner,uint64_t generation) noexcept {
    if(!Owns(owner,generation))return false;
    exit_pending_=true;return true;
  }
  template<class Exit>
  bool ConfirmExit(uint32_t owner,uint64_t generation,bool confirm,Exit&& exit) {
    if(!Owns(owner,generation) || !exit_pending_)return false;
    exit_pending_=false;
    if(!confirm)return true;
    Leave();exit();return true;
  }
 private:
  uint32_t owner_=0;
  uint64_t generation_=0;
  bool exit_pending_=false;
};
} // namespace hulk::options
