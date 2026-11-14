// Atomic, bounded edit of the ONE existing NativePC TOML file.
// Independent of RexGlue and of GPU/guest execution.
#include "native_pc_options_store.h"
#include "native_pc_bindings.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
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

bool Allowed(std::string_view key, std::string_view value) {
  // Exact source-supported native mouse/keyboard bindings, not arbitrary
  // guest flags. All token values are single, known BindTokenDown names.
  if (IsBindingOption(key)) return IsBindingToken(value);
  if (key == "vsync") return value == "true" || value == "false";
  if (key == "hulk_guest_swap_fps_cap")
    return value == "0" || value == "30" || value == "60" ||
           value == "90" || value == "120" || value == "144";
  if (key == "native_x_renderer_resolution_scale")
    return value == "1.0" || value == "2.0";
  if (key == "native_x_renderer_scaling")
    return value == "bilinear" || value == "fsr" || value == "nis";
  if (key == "native_x_renderer_sharpness") {
    if (value.empty() || value.size() > 24) return false;
    const std::string copy(value);
    char* end = nullptr;
    const double number = std::strtod(copy.c_str(), &end);
    return end == copy.c_str() + copy.size() &&
           std::isfinite(number) && number >= 0.0 && number <= 0.5;
  }
  return false;
}

bool IsAssignment(std::string_view line, std::string_view key) {
  const auto start = line.find_first_not_of(" \t");
  if (start == std::string_view::npos) return false;
  line.remove_prefix(start);
  if (!line.starts_with(key)) return false;
  line.remove_prefix(key.size());
  const auto p = line.find_first_not_of(" \t");
  return p != std::string_view::npos && line[p] == '=';
}

}  // namespace

bool Save(const std::filesystem::path& config_file,
          std::string_view key, std::string_view value) noexcept {
  try {
    if (!Allowed(key, value) || config_file.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(config_file, ec) || ec ||
        std::filesystem::file_size(config_file, ec) > (2u << 20) || ec)
      return false;
    std::ifstream in(config_file, std::ios::binary);
    if (!in) return false;
    const std::string original(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return false;
    const bool crlf = original.find("\r\n") != std::string::npos;
    const std::string ending = crlf ? "\r\n" : "\n";
    std::string new_value(value);
    if (key == "native_x_renderer_scaling" || IsBindingOption(key))
      new_value = "\"" + new_value + "\"";
    const std::string assignment = std::string(key) + " = " + new_value;

    std::istringstream lines(original);
    std::string line, edited;
    unsigned matches = 0;
    while (std::getline(lines, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (IsAssignment(line, key)) {
        edited += assignment;
        ++matches;
      } else {
        edited += line;
      }
      edited += ending;
    }
    // Refuse malformed duplicate assignments instead of inventing which
    // one ReXGlue's TOML reader will use.
    if (matches > 1) return false;
    if (matches == 0) edited += assignment + ending;
    if (edited == original) return true;

    auto temporary = config_file;
    temporary += ".nativepc-writing";
    {
      std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
      if (!out) return false;
      out.write(edited.data(), static_cast<std::streamsize>(edited.size()));
      out.flush();
      if (!out) {
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
  } catch (...) {
    return false;
  }
}

}  // namespace hulk::options
