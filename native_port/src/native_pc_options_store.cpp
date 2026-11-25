// Atomic, bounded edit of the ONE existing NativePC TOML file.
// Independent of RexGlue and of GPU/guest execution.
#include "native_pc_options_store.h"
#include "native_config_write_mutex.h"
#include "native_audio_options.h"
#include "native_pc_bindings.h"
#include "native_resolution.h"

#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hulk::options {
namespace {

// Menu values are unsigned decimal integers or fixed-point decimals.
// Unlike strtod, this excludes hexadecimal floats, locale-dependent commas,
// whitespace and malformed TOML such as .5, 01 or 1.
bool ValidDecimal(std::string_view value,double minimum,double maximum) {
  if(value.empty() || value.size()>24)return false;
  const auto dot=value.find('.');
  const auto whole=value.substr(0,dot);
  if(whole.empty() || (whole.size()>1 && whole.front()=='0'))return false;
  const auto digits=[](std::string_view part) {
    if(part.empty())return false;
    for(char c:part)if(c<'0' || c>'9')return false;
    return true;
  };
  if(!digits(whole) ||
     (dot!=std::string_view::npos && !digits(value.substr(dot+1))))return false;
  double number=0;
  const auto result=std::from_chars(value.data(),value.data()+value.size(),
                                    number,std::chars_format::fixed);
  return result.ec==std::errc{} && result.ptr==value.data()+value.size() &&
         std::isfinite(number) && number>=minimum && number<=maximum;
}

bool Allowed(std::string_view key, std::string_view value) {
  // Exact source-supported native mouse/keyboard bindings, not arbitrary
  // guest flags. All token values are single, known BindTokenDown names.
  if (IsBindingOption(key)) return IsBindingToken(value);
  if (IsHostAudioOption(key)) return IsHostAudioValue(key,value);
  if (key == "vsync" || key == "fullscreen" ||
      key == "hulk_original_havok_catchup" ||
      key == "hulk_eor_vulkan_replay" ||
      key == "hulk_eor_xdk_capture" ||
      key == "hulk_eor_xdk_source_mesh_capture" ||
      key == "hulk_mouse_invert_x" || key == "hulk_mouse_invert_y")
    return value == "true" || value == "false";
  if (key == "anisotropic_override")
    return value=="-1" || value=="0" || value=="1" || value=="2" ||
           value=="3" || value=="4" || value=="5";
  if(key=="hulk_original_music_volume" || key=="hulk_original_sfx_volume" ||
     key=="hulk_original_dialog_volume") {
    if(value=="-1")return true;
    if(value.empty() || value.size()>3 ||
       value.front()<'0' || value.front()>'9' ||
       (value.size()>1 && value.front()=='0'))return false;
    uint32_t volume=0;
    const auto result=std::from_chars(value.data(),value.data()+value.size(),volume);
    return result.ec==std::errc{} && result.ptr==value.data()+value.size() && volume<=100;
  }
  if (key == "hulk_original_subtitles")
    return value == "-1" || value == "0" || value == "1";
  if (key == "hulk_guest_swap_fps_cap")
    return value == "0" || value == "30" || value == "60" ||
           value == "90" || value == "120" || value == "144";
  if (key == "native_x_renderer_resolution_scale")
    return value == "0.0" || value == "1.0" || value == "2.0";
  if (key == "native_x_renderer_scaling")
    return value == "bilinear" || value == "fsr" || value == "nis";
  if (key == "native_x_renderer_antialiasing")
    return value == "off" || value == "fxaa" || value == "smaa";
  if (key == "hulk_mouse_horizontal_multiplier" ||
      key == "hulk_mouse_vertical_multiplier") {
    return ValidDecimal(value,0.25,4.0);
  }
  if (key == "native_x_renderer_sharpness") {
    return ValidDecimal(value,0.0,0.5);
  }
  return false;
}

bool IsAssignment(std::string_view line, std::string_view key) {
  // Accept a UTF-8 BOM on the first line but never create a second key.
  if (line.size() >= 3 &&
      static_cast<unsigned char>(line[0]) == 0xef &&
      static_cast<unsigned char>(line[1]) == 0xbb &&
      static_cast<unsigned char>(line[2]) == 0xbf)
    line.remove_prefix(3);
  const auto start = line.find_first_not_of(" \t");
  if (start == std::string_view::npos) return false;
  line.remove_prefix(start);
  if (!line.starts_with(key)) return false;
  line.remove_prefix(key.size());
  const auto p = line.find_first_not_of(" \t");
  return p != std::string_view::npos && line[p] == '=';
}

using Setting = std::pair<std::string_view, std::string_view>;
std::mutex g_live_path_mutex;
std::filesystem::path g_live_config_path;

// Atomic one-file update of 1-2 whitelisted settings. Used for both
// single controls and a paired window resolution to avoid half-saved
// W/H dimensions after disk or permission errors.
bool SaveSettings(const std::filesystem::path& config_file,
                  std::span<const Setting> updates) {
  if (config_file.empty() || updates.empty() || updates.size() > 2)
    return false;
  // The original guest UI can save from the game thread while the host
  // menu writes from the window thread. Serialize the entire read/edit/
  // atomic replace, not just the final rename of the common temp file.
  std::lock_guard lock(hulk::config::WriteMutex());
  std::error_code ec;
  if (!std::filesystem::is_regular_file(config_file, ec) || ec ||
      std::filesystem::file_size(config_file, ec) > (2u << 20) || ec)
    return false;
  std::ifstream in(config_file, std::ios::binary);
  if (!in) return false;
  const std::string original(
      (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) return false;
  // On Windows a live fstream without FILE_SHARE_DELETE prevents the
  // atomic MoveFileExW replacement of its own source path.
  in.close();
  if (!in) return false;
  const bool crlf = original.find("\r\n") != std::string::npos;
  const std::string ending = crlf ? "\r\n" : "\n";
  const bool bom = original.starts_with("\xEF\xBB\xBF");
  std::array<unsigned, 2> matched{};
  std::istringstream lines(original);
  std::string line, edited;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    bool replaced = false;
    for (size_t j = 0; j < updates.size(); ++j) {
      const auto& [key, value] = updates[j];
      if (!IsAssignment(line, key)) continue;
      if (++matched[j] > 1) return false;
      // The original byte-order mark belongs to the FILE, not to the
      // first setting: preserve it if that setting is replaced.
      if (edited.empty() && bom)
        edited += "\xEF\xBB\xBF";
      edited += std::string(key) + " = ";
      if (key == "native_x_renderer_scaling" || key == "native_x_renderer_antialiasing" || IsBindingOption(key))
        edited += "\"" + std::string(value) + "\"";
      else
        edited += value;
      replaced = true;
      break;
    }
    if (!replaced) edited += line;
    edited += ending;
  }
  for (size_t j = 0; j < updates.size(); ++j) {
    if (matched[j]) continue;
    const auto& [key, value] = updates[j];
    edited += std::string(key) + " = ";
    if (key == "native_x_renderer_scaling" || key == "native_x_renderer_antialiasing" || IsBindingOption(key))
      edited += "\"" + std::string(value) + "\"";
    else
      edited += value;
    edited += ending;
  }
  if (edited == original) return true;
  if (edited.size() > (2u << 20)) return false;
  auto temporary = config_file;
  temporary += ".nativepc-writing";
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(edited.data(), static_cast<std::streamsize>(edited.size()));
    out.flush();
    const bool flushed=out.good();
    out.close();
    if (!flushed || !out.good()) {
      std::filesystem::remove(temporary, ec);
      return false;
    }
  }
#ifdef _WIN32
  const bool replaced =
      MoveFileExW(temporary.c_str(), config_file.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::filesystem::rename(temporary, config_file, ec);
  const bool replaced = !ec;
#endif
  if (!replaced) std::filesystem::remove(temporary, ec);
  return replaced;
}
}  // namespace

bool Save(const std::filesystem::path& config_file,
          std::string_view key, std::string_view value) noexcept {
  try {
    if (!Allowed(key, value)) return false;
    const std::array<Setting, 1> single{{{key, value}}};
    return SaveSettings(config_file, single);
  } catch (...) {
    return false;
  }
}

bool SaveWindowSize(const std::filesystem::path& config_file,
                    uint32_t width, uint32_t height) noexcept {
  try {
    if (!display::IsSupportedHostSize(width, height)) return false;
    // Freeform host output: any validated monitor/custom pixel dimensions.
    // Unlike the generic SDK `resolution` CVar, these two change
    // ONLY the host window, leaving the guest 1280x720 XDK descriptors
    // and PM4 tiling/resolve dimensions exactly as they were.
    const auto w = std::to_string(width);
    const auto h = std::to_string(height);
    const std::array<Setting, 2> pair{{
        {"window_width", w}, {"window_height", h},
    }};
    return SaveSettings(config_file, pair);
  } catch (...) {
    return false;
  }
}

void SetLiveConfigPath(const std::filesystem::path& path) noexcept {
  try {
    std::lock_guard lock(g_live_path_mutex);
    g_live_config_path=path;
  } catch (...) {}
}
bool SaveLiveOption(std::string_view key,std::string_view value) noexcept {
  try {
    std::filesystem::path path;
    { std::lock_guard lock(g_live_path_mutex); path=g_live_config_path; }
    return Save(path,key,value);
  } catch (...) {return false;}
}
bool SaveLiveWindowSize(uint32_t width,uint32_t height) noexcept {
  try {
    std::filesystem::path path;
    { std::lock_guard lock(g_live_path_mutex); path=g_live_config_path; }
    return SaveWindowSize(path,width,height);
  } catch (...) {return false;}
}
bool SaveLiveSourceMeshCapture(bool enabled) noexcept {
  if(!enabled)return SaveLiveOption("hulk_eor_xdk_source_mesh_capture","false");
  try {
    std::filesystem::path path;
    { std::lock_guard lock(g_live_path_mutex); path=g_live_config_path; }
    const std::array<Setting,2> pair{{
        {"hulk_eor_xdk_source_mesh_capture","true"},
        {"hulk_eor_xdk_capture","true"}
    }};
    return SaveSettings(path,pair);
  } catch (...) {return false;}
}
bool SaveOriginalAudioVolume(std::string_view key,uint32_t value) noexcept {
  if(value>100 || (key!="hulk_original_music_volume" &&
      key!="hulk_original_sfx_volume" && key!="hulk_original_dialog_volume"))return false;
  try {
    return SaveLiveOption(key,std::to_string(value));
  } catch (...) {return false;}
}
bool SaveOriginalSubtitles(bool enabled) noexcept {
  try {
    return SaveLiveOption("hulk_original_subtitles",enabled?"1":"0");
  } catch (...) {return false;}
}
bool SaveOriginalLookInversion(std::optional<bool> yaw,
                               std::optional<bool> pitch) noexcept {
  try {
    if(!yaw && !pitch)return true;
    std::filesystem::path path;
    {
      std::lock_guard lock(g_live_path_mutex);
      path=g_live_config_path;
    }
    std::array<Setting,2> updates{};
    size_t count=0;
    if(yaw)updates[count++]={"hulk_mouse_invert_x",*yaw?"true":"false"};
    if(pitch)updates[count++]={"hulk_mouse_invert_y",*pitch?"true":"false"};
    return SaveSettings(path,std::span<const Setting>(updates.data(),count));
  } catch (...) {return false;}
}

}  // namespace hulk::options
