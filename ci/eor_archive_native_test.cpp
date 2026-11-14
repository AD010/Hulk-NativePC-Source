// Standalone, native x64 EoR resource tests. NO private game files.
// Compile via MSVC on the existing Windows workflow before private codegen.
#include "../native_port/src/eor_archive.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using hulk::eor::ByteOrder;
using hulk::eor::Index;
using hulk::eor::ReadResource;
using hulk::eor::Resource;
using hulk::eor::ResourceHash;
using Bytes = std::vector<std::byte>;

namespace {

void Append32(Bytes& out, uint32_t value, ByteOrder order) {
  for (int i = 0; i < 4; ++i) {
    const unsigned shift = static_cast<unsigned>(
        order == ByteOrder::Little ? i * 8 : (3 - i) * 8);
    out.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
  }
}
void Set32(Bytes& out, size_t pos, uint32_t value, ByteOrder order) {
  for (int i = 0; i < 4; ++i) {
    const unsigned shift = static_cast<unsigned>(
        order == ByteOrder::Little ? i * 8 : (3 - i) * 8);
    out.at(pos + static_cast<size_t>(i)) =
        static_cast<std::byte>((value >> shift) & 0xffu);
  }
}

Bytes MakeIndex(ByteOrder order, bool corrupt_sorted = false) {
  // 2 archive groups -> 4 named/table blocks -> 5 u32 offset entries.
  Bytes result;
  Append32(result, 4, order);
  for (int i = 0; i < 5; ++i) Append32(result, 0, order);
  std::array<uint32_t, 5> positions{};

  positions[0] = static_cast<uint32_t>(result.size());
  for (unsigned char c : std::string("Fonts")) result.push_back(std::byte{c});
  result.push_back(std::byte{0});
  positions[1] = static_cast<uint32_t>(result.size());
  Append32(result, 1, order);
  Append32(result, ResourceHash("font_clean"), order);
  Append32(result, 4, order);
  Append32(result, 8, order);
  Append32(result, 0, order);

  positions[2] = static_cast<uint32_t>(result.size());
  for (unsigned char c : std::string("Textures")) result.push_back(std::byte{c});
  result.push_back(std::byte{0});
  positions[3] = static_cast<uint32_t>(result.size());
  Append32(result, 2, order);
  const uint32_t a = ResourceHash("ui_menuitems_da");
  const uint32_t b = ResourceHash("ui_key_wasd_da");
  if (corrupt_sorted) {
    Append32(result, b, order);
    Append32(result, a, order);
  } else {
    Append32(result, a, order);
    Append32(result, b, order);
  }
  Append32(result, 12, order);
  Append32(result, 6, order);
  Append32(result, 0, order);
  Append32(result, 18, order);
  Append32(result, 4, order);
  Append32(result, 0, order);

  positions[4] = static_cast<uint32_t>(result.size());
  Append32(result, 1000, order);
  for (size_t i = 0; i < positions.size(); ++i)
    Set32(result, 4 + i * 4, positions[i], order);
  return result;
}

template <typename F>
void MustThrow(F operation) {
  bool threw = false;
  try { operation(); } catch (const std::exception&) { threw = true; }
  if (!threw) throw std::runtime_error("Expected malformed EoR data rejection");
}
void Expect(bool result, const char* description) {
  if (!result) throw std::runtime_error(description);
}

}  // namespace

int main() {
  const std::array<std::pair<const char*, uint32_t>, 11> ids{{
      {"_app_", 0x721AAFDEu},
      {"_engine_", 0xCA54CBC0u},
      {"_shader_effects_", 0x2485AB96u},
      {"font_clean", 0xA796ECFAu},
      {"TXFLui_menuitems_da", 0x2F95A2B1u},
      {"TXFLui_menubands_da", 0x20CE9F3Cu},
      {"TXFLui_key_wasd_da", 0x6274200Au},
      {"TXFLui_key_space_da", 0xD813EA9Au},
      {"LFXTui_xbox360_a_da", 0x16FEB6A7u},
      {"LFXTui_xbox360_rt_da", 0x063884F1u},
      {"TXFLui_mouse_leftbutton_da", 0x79FFD3E8u},
  }};
  for (auto [name, expected] : ids)
    Expect(ResourceHash(name) == expected, "CRC32 EoR name/hash mismatch");
  MustThrow([] { (void)ResourceHash("../untrusted"); });
  MustThrow([] { (void)ResourceHash("ui\\malformed"); });
  MustThrow([] { (void)ResourceHash(""); });

  for (auto order : {ByteOrder::Little, ByteOrder::Big}) {
    auto bytes = MakeIndex(order);
    Index index = Index::Decode(bytes);
    Expect(index.byte_order() == order, "EoR endianness differs");
    Expect(index.footer() == 1000, "EoR footer corrupted");
    Expect(index.groups().size() == 2, "Expected two EoR archive groups");
    auto font = index.FindName("Fonts", "font_clean");
    Expect(font.has_value() && font->offset == 4 && font->size == 8,
           "EoR font asset missing");
    auto tex = index.FindName("Textures", "TXFLui_menuitems_da");
    Expect(tex.has_value() && tex->offset == 12 && tex->size == 6,
           "EoR normalized typed texture ID missing");
    Expect(!index.FindName("Textures", "does_not_exist").has_value(),
           "EoR unknown name should not match");
    MustThrow([&] { (void)Index::Decode(MakeIndex(order, true)); });
    auto truncated = bytes;
    truncated.pop_back();
    MustThrow([&] { (void)Index::Decode(truncated); });
    auto malformed = bytes;
    malformed[0] = std::byte{0};
    malformed[1] = std::byte{0};
    malformed[2] = std::byte{0};
    malformed[3] = std::byte{0};
    MustThrow([&] { (void)Index::Decode(malformed); });
  }

  const auto temp = std::filesystem::temp_directory_path() /
                    "hulk_eor_native_no_game_data.arc";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    const char bytes[] = "XXXXFONT-ONEOTHER";
    out.write(bytes, static_cast<std::streamsize>(sizeof(bytes) - 1));
  }
  const Resource valid{ResourceHash("font_clean"), 4, 8, 0};
  auto data = ReadResource(temp, valid);
  Expect(data.size() == 8, "EoR extracted payload length mismatch");
  const char expected[] = "FONT-ONE";
  for (size_t i = 0; i < data.size(); ++i)
    Expect(data[i] == static_cast<std::byte>(expected[i]),
           "EoR extracted payload bytes differ");
  MustThrow([&] { (void)ReadResource(temp, Resource{0, 99999, 8, 0}); });
  MustThrow([&] { (void)ReadResource(temp, Resource{0, 4, 8, 0}, 7); });
  MustThrow([&] { (void)ReadResource(temp, Resource{0, 0, 8, 0}); });
  std::filesystem::remove(temp);

  std::cout << "[HULK/EOR-NATIVE] indexed PC+Xbox font and UI assets: PASS\n";
  return 0;
}
