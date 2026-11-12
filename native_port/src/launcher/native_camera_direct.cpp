#define _CRT_SECURE_NO_WARNINGS
#include "native_camera_direct.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hulk::camera {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Synthetic layout for unit tests ONLY.
// Not addresses discovered in Hulk's PPC executable.
constexpr Layout kTestLayout{
    0x100, 0x20, 0x24,
    0.002f, -1.4f, 1.4f, false
};

struct Settings {
  Layout layout{};
  bool ready = false;
};

uint32_t ReadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) |
         (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) |
         uint32_t(p[3]);
}

void WriteBE32(uint8_t* p, uint32_t value) {
  p[0] = uint8_t(value >> 24);
  p[1] = uint8_t(value >> 16);
  p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

float ReadBEFloat(const uint8_t* p) {
  return std::bit_cast<float>(ReadBE32(p));
}

void WriteBEFloat(uint8_t* p, float value) {
  WriteBE32(p, std::bit_cast<uint32_t>(value));
}

bool InBounds(size_t size, uint64_t address, size_t length) {
  return address <= size && length <= size - address;
}

std::filesystem::path ProfilePath() {
  if (const char* env = std::getenv("HULK_DIRECT_CAMERA_PROFILE");
      env && *env)
    return env;

#ifdef _WIN32
  wchar_t path[32768]{};
  DWORD length = GetModuleFileNameW(nullptr, path, 32768);
  if (length && length < 32768)
    return std::filesystem::path(path).parent_path() /
           "hulk_direct_camera.ini";
#endif

  return "hulk_direct_camera.ini";
}

void Trim(std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    s.clear();
    return;
  }
  const auto last = s.find_last_not_of(" \t\r\n");
  s = s.substr(first, last - first + 1);
}

bool ParseUint(const std::string& s, uint32_t& value) {
  try {
    size_t end = 0;
    auto n = std::stoull(s, &end, 0);
    if (end != s.size() || n > UINT32_MAX)
      return false;
    value = static_cast<uint32_t>(n);
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseFloat(const std::string& s, float& value) {
  try {
    size_t end = 0;
    const float n = std::stof(s, &end);
    if (end != s.size() || !std::isfinite(n))
      return false;
    value = n;
    return true;
  } catch (...) {
    return false;
  }
}

Settings LoadSettings() {
  Settings result{};
  std::ifstream file(ProfilePath());
  if (!file)
    return result;

  bool havePointer = false;
  bool haveYaw = false;
  bool havePitch = false;
  std::string line;

  while (std::getline(file, line)) {
    const auto comment = line.find_first_of("#;");
    if (comment != std::string::npos)
      line.resize(comment);

    const auto equal = line.find('=');
    if (equal == std::string::npos)
      continue;

    std::string key = line.substr(0, equal);
    std::string value = line.substr(equal + 1);
    Trim(key);
    Trim(value);

    if (value.empty())
      return {};

    auto& c = result.layout;

    if (key == "pointer_slot") {
      if (!ParseUint(value, c.pointer_slot))
        return {};
      havePointer = true;
    } else if (key == "yaw_offset") {
      if (!ParseUint(value, c.yaw_offset))
        return {};
      haveYaw = true;
    } else if (key == "pitch_offset") {
      if (!ParseUint(value, c.pitch_offset))
        return {};
      havePitch = true;
    } else if (key == "radians_per_count") {
      if (!ParseFloat(value, c.radians_per_count))
        return {};
    } else if (key == "pitch_min") {
      if (!ParseFloat(value, c.pitch_min))
        return {};
    } else if (key == "pitch_max") {
      if (!ParseFloat(value, c.pitch_max))
        return {};
    } else if (key == "invert_y") {
      if (value != "0" && value != "1")
        return {};
      c.invert_y = value == "1";
    } else {
      return {};
    }
  }

  const auto& c = result.layout;

  result.ready =
      havePointer && haveYaw && havePitch &&
      c.pointer_slot >= 0x80000000u &&
      c.yaw_offset <= 4096 &&
      c.pitch_offset <= 4096 &&
      c.yaw_offset != c.pitch_offset &&
      c.radians_per_count > 0.000001f &&
      c.radians_per_count <= 0.05f &&
      c.pitch_min >= -kPi &&
      c.pitch_max <= kPi &&
      c.pitch_min < c.pitch_max;

  return result;
}

#ifdef _WIN32

uint8_t* GuestAddress(
    uint8_t* base, uint32_t guest,
    size_t bytes, bool writable) {

  if (!base || !bytes)
    return nullptr;

  const uint64_t offset =
      uint64_t(guest) +
      (guest >= 0xE0000000u ? 4096u : 0u);

  if (offset > UINTPTR_MAX - uintptr_t(base))
    return nullptr;

  auto* p = reinterpret_cast<uint8_t*>(
      uintptr_t(base) + uintptr_t(offset));

  MEMORY_BASIC_INFORMATION info{};
  if (VirtualQuery(p, &info, sizeof(info)) != sizeof(info))
    return nullptr;

  if (info.State != MEM_COMMIT ||
      (info.Protect & PAGE_GUARD))
    return nullptr;

  const DWORD prot = info.Protect & 0xFF;
  const bool readable =
      prot == PAGE_READONLY ||
      prot == PAGE_READWRITE ||
      prot == PAGE_WRITECOPY ||
      prot == PAGE_EXECUTE_READ ||
      prot == PAGE_EXECUTE_READWRITE ||
      prot == PAGE_EXECUTE_WRITECOPY;

  const bool canWrite =
      prot == PAGE_READWRITE ||
      prot == PAGE_WRITECOPY ||
      prot == PAGE_EXECUTE_READWRITE ||
      prot == PAGE_EXECUTE_WRITECOPY;

  if (!readable || (writable && !canWrite))
    return nullptr;

  const uintptr_t start = uintptr_t(p);
  const uintptr_t end =
      uintptr_t(info.BaseAddress) + info.RegionSize;

  if (start > end || bytes > end - start)
    return nullptr;

  return p;
}
#endif

} // namespace

Angles ApplyDelta(
    Angles angles, int dx, int dy,
    const Layout& c, float sensitivity_scale) {

  if (!std::isfinite(angles.yaw) ||
      !std::isfinite(angles.pitch))
    return angles;

  // HULK/DIRECT-CAMERA-SENSITIVITY: user settings apply to direct yaw/pitch
  // without manufacturing an Xbox right-stick deflection or rescaling time.
  const float scale = std::isfinite(sensitivity_scale)
      ? std::clamp(sensitivity_scale, 0.025f, 5.0f) : 1.0f;
  const float x =
      float(std::clamp(dx, -12000, 12000)) *
      c.radians_per_count * scale;

  const float y =
      float(std::clamp(dy, -12000, 12000)) *
      c.radians_per_count * scale *
      (c.invert_y ? -1.0f : 1.0f);

  angles.yaw =
      std::remainder(angles.yaw + x, 2.0f * kPi);

  angles.pitch = std::clamp(
      angles.pitch + y,
      c.pitch_min, c.pitch_max);

  return angles;
}

bool ApplyToGuestSpan(
    uint8_t* memory, size_t size,
    const Layout& c, int dx, int dy, float sensitivity_scale) {

  if (!memory || (!dx && !dy) ||
      !InBounds(size, c.pointer_slot, 4))
    return false;

  const uint32_t object =
      ReadBE32(memory + c.pointer_slot);

  const uint64_t yawAddress =
      uint64_t(object) + c.yaw_offset;

  const uint64_t pitchAddress =
      uint64_t(object) + c.pitch_offset;

  if (!object ||
      !InBounds(size, yawAddress, 4) ||
      !InBounds(size, pitchAddress, 4))
    return false;

  uint8_t* yaw = memory + yawAddress;
  uint8_t* pitch = memory + pitchAddress;

  const Angles previous{
      ReadBEFloat(yaw), ReadBEFloat(pitch)
  };

  if (!std::isfinite(previous.yaw) ||
      !std::isfinite(previous.pitch) ||
      previous.pitch < -4.0f ||
      previous.pitch > 4.0f)
    return false;

  const Angles next = ApplyDelta(previous, dx, dy, c, sensitivity_scale);
  WriteBEFloat(yaw, next.yaw);
  WriteBEFloat(pitch, next.pitch);
  return true;
}

bool ApplyToGameMemory(
    uint8_t* base, int dx, int dy, float sensitivity_scale) {

  static const Settings settings = LoadSettings();

  if (!settings.ready || !base || (!dx && !dy))
    return false;

#ifdef _WIN32
  const auto& c = settings.layout;

  uint8_t* slot =
      GuestAddress(base, c.pointer_slot, 4, false);

  if (!slot)
    return false;

  const uint32_t object = ReadBE32(slot);
  if (!object)
    return false;

  const uint64_t yawAddress =
      uint64_t(object) + c.yaw_offset;

  const uint64_t pitchAddress =
      uint64_t(object) + c.pitch_offset;

  if (yawAddress > UINT32_MAX ||
      pitchAddress > UINT32_MAX)
    return false;

  uint8_t* yaw = GuestAddress(
      base, uint32_t(yawAddress), 4, true);

  uint8_t* pitch = GuestAddress(
      base, uint32_t(pitchAddress), 4, true);

  if (!yaw || !pitch)
    return false;

  const Angles previous{
      ReadBEFloat(yaw), ReadBEFloat(pitch)
  };

  if (!std::isfinite(previous.yaw) ||
      !std::isfinite(previous.pitch) ||
      previous.pitch < -4.0f ||
      previous.pitch > 4.0f)
    return false;

  const Angles next = ApplyDelta(previous, dx, dy, c, sensitivity_scale);
  WriteBEFloat(yaw, next.yaw);
  WriteBEFloat(pitch, next.pitch);
  return true;
#else
  (void)sensitivity_scale;
  return false;
#endif
}

} // namespace hulk::camera
