// Standalone, native x64 EoR resource tests. NO private game files.
// Compile via MSVC on the existing Windows workflow before private codegen.
#include "../native_port/src/eor_archive.h"
#include "../native_port/src/eor_ui_asset_catalog.h"

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
  // Original UI material/font catalogue: prove both platforms stay
  // tagged by source, and Xbox-specific bytes are never silently
  // reinterpreted as PC graphics without a real texture decoder.
  const auto dir = temp.parent_path();
  const auto pc_index_file = dir / "hulk-eor-ui-pc-synthetic.ind";
  const auto xbox_index_file = dir / "hulk-eor-ui-xbox-synthetic.ind";
  const auto pc_tex = dir / "hulk-eor-ui-pc-synthetic.arc";
  const auto xbox_tex = dir / "hulk-eor-ui-xbox-synthetic.arc";
  {
    const auto pc = MakeIndex(ByteOrder::Little);
    const auto xbox = MakeIndex(ByteOrder::Big);
    std::ofstream a(pc_index_file, std::ios::binary);
    a.write(reinterpret_cast<const char*>(pc.data()),
            static_cast<std::streamsize>(pc.size()));
    std::ofstream b(xbox_index_file, std::ios::binary);
    b.write(reinterpret_cast<const char*>(xbox.data()),
            static_cast<std::streamsize>(xbox.size()));
  }
  {
    std::ofstream out(pc_tex, std::ios::binary);
    const std::string src = std::string(12, '0') + "DDS X_" + "abcd";
    out.write(src.data(), static_cast<std::streamsize>(src.size()));
  }
  {
    std::ofstream out(xbox_tex, std::ios::binary);
    const std::string src = std::string(12, '0') + "GUEST!" + "ZYXW";
    out.write(src.data(), static_cast<std::streamsize>(src.size()));
  }
  using hulk::eor::OriginalArchiveSet;
  using hulk::eor::OriginalPlatform;
  using hulk::eor::OriginalUiAssets;
  using hulk::eor::KnownImageEncoding;
  using hulk::eor::ClassifyImage;
  auto original = OriginalUiAssets::Open(
      OriginalArchiveSet{pc_index_file, temp, pc_tex},
      OriginalArchiveSet{xbox_index_file, temp, xbox_tex});
  Expect(original.HasXboxSource(), "Xbox original source not indexed");
  auto pc_ui = original.Find(OriginalPlatform::Pc2008, "Textures",
                             "TXFLui_menuitems_da");
  auto xbox_ui = original.Find(OriginalPlatform::Xbox360, "Textures",
                               "LFXTui_menuitems_da");
  Expect(pc_ui.has_value() && xbox_ui.has_value(),
         "Original EoR menu material hash not resolved on both platforms");
  Expect(pc_ui->record.hash == xbox_ui->record.hash,
         "Original EoR menu material lost identity across platforms");
  Expect(pc_ui->platform != xbox_ui->platform,
         "Console and PC provenance incorrectly merged");
  Expect(ClassifyImage(original.Read(*pc_ui)) == KnownImageEncoding::Dds,
         "Positive PC DDS magic detection failed");
  Expect(ClassifyImage(original.Read(*xbox_ui)) == KnownImageEncoding::Unknown,
         "Unknown Xbox texture was incorrectly claimed to be host DDS");
  auto spoofed = *pc_ui;
  spoofed.archive = xbox_tex;
  MustThrow([&] { (void)original.Read(spoofed); });
  spoofed = *pc_ui;
  spoofed.record.size = 10;
  MustThrow([&] { (void)original.Read(spoofed); });
  Expect(!original.Find(OriginalPlatform::Pc2008, "Textures",
                         "not_an_asset").has_value(),
         "Unknown original UI resource should not resolve");
  Expect(!original.Find(OriginalPlatform::Pc2008, "Objects",
                         "ui_menuitems_da").has_value(),
         "Unbound original resource archive should not be guessed");
  auto pc_only = OriginalUiAssets::Open(
      OriginalArchiveSet{pc_index_file, temp, pc_tex});
  Expect(!pc_only.Find(OriginalPlatform::Xbox360, "Textures",
                        "ui_menuitems_da").has_value(),
         "Xbox texture missing should not be transparently replaced by PC");
  MustThrow([&] {
    (void)OriginalUiAssets::Open(
        OriginalArchiveSet{xbox_index_file, temp, pc_tex});
  });
  std::filesystem::remove(pc_index_file);
  std::filesystem::remove(xbox_index_file);
  std::filesystem::remove(pc_tex);
  std::filesystem::remove(xbox_tex);
  std::filesystem::remove(temp);

  std::cout << "[HULK/EOR-NATIVE] indexed PC+Xbox font and UI assets: PASS\n";
  return 0;
}
