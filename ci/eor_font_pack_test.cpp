// Synthetic-only source regression for the original EoR font_clean layout.
// Actual font bytes are never checked into the public repository.
#include "../native_port/src/eor_font_pack.h"
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <limits>
#include <string_view>
#include <vector>

using namespace hulk::eor;
using Data = std::vector<std::byte>;
void Put32(Data& data, uint32_t value, ByteOrder order) {
  for (unsigned i = 0; i < 4; ++i)
    data.push_back(std::byte((value >> (order == ByteOrder::Big ?
                         24u - i * 8u : i * 8u)) & 255u));
}
void Set32(Data& data, size_t offset, uint32_t value, ByteOrder order) {
  for (unsigned i = 0; i < 4; ++i)
    data.at(offset + i) = std::byte((value >> (order == ByteOrder::Big ?
                                 24u - i * 8u : i * 8u)) & 255u);
}
Data Make(ByteOrder order, unsigned profiles) {
  Data data;
  Put32(data, 2u * profiles, order);
  for (unsigned i = 0; i <= 2u * profiles; ++i) Put32(data, 0u, order);
  std::vector<uint32_t> offsets;
  for (unsigned i = 0; i < profiles; ++i) {
    offsets.push_back(uint32_t(data.size()));
    Put32(data, 1280u - i * 40u, order);
    Put32(data, 720u - i * 20u, order);
    Put32(data, std::bit_cast<uint32_t>(1.333f), order);
  }
  for (unsigned i = 0; i < profiles; ++i) {
    offsets.push_back(uint32_t(data.size()));
    constexpr std::string_view name = "font_clean";
    for (char c : name) data.push_back(std::byte(uint8_t(c)));
    data.push_back(std::byte{0});
    data.push_back(std::byte(uint8_t(i)));
  }
  offsets.push_back(uint32_t(data.size()));
  for (size_t i = 0; i < offsets.size(); ++i)
    Set32(data, 4u + i * 4u, offsets[i], order);
  return data;
}
template<class F> void Bad(F operation) {
  bool failed = false;
  try { operation(); } catch (const std::invalid_argument&) { failed = true; }
  if (!failed) throw std::runtime_error("Unsafe EoR font resource accepted");
}
int main() {
  for (auto order : {ByteOrder::Little, ByteOrder::Big}) {
    for (unsigned n : {9u, 10u}) {
      auto data = Make(order, n);
      const auto pack = DecodeFontPack(data, order);
      assert(pack.profiles.size() == n);
      assert(pack.profiles[0].width == 1280u);
      assert(pack.profiles[0].height == 720u);
      assert(std::abs(pack.profiles[0].profile_parameter - 1.333f) < 0.0001f);
      for (const auto& entry : pack.profiles)
        assert(entry.data_offset + entry.data_length <= data.size());

      auto corrupt = data;
      corrupt[pack.profiles[0].data_offset] = std::byte{'X'};
      Bad([&]{ (void)DecodeFontPack(corrupt, order); });
      corrupt = data;
      corrupt.resize(12);
      Bad([&]{ (void)DecodeFontPack(corrupt, order); });
      corrupt = data;
      Set32(corrupt, 0, 3u, order);
      Bad([&]{ (void)DecodeFontPack(corrupt, order); });
      corrupt = data;
      Set32(corrupt, 4, uint32_t(corrupt.size() + 10u), order);
      Bad([&]{ (void)DecodeFontPack(corrupt, order); });
      corrupt = data;
      Set32(corrupt, 4u + size_t(n) * 4u,
            uint32_t(corrupt.size() + 1u), order);
      Bad([&]{ (void)DecodeFontPack(corrupt, order); });
      corrupt = data;
      Set32(corrupt, 4u + (2u * n) * 4u,
            uint32_t(corrupt.size() - 1u), order);
      Bad([&]{ (void)DecodeFontPack(corrupt, order); });
      corrupt = data;
      Set32(corrupt, pack.profiles[0].data_offset - 12u,
            std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN()), order);
      Bad([&]{ (void)DecodeFontPack(corrupt, order); });
      // Decoding another platform's byte order must not silently succeed.
      Bad([&]{ (void)DecodeFontPack(data, order == ByteOrder::Little ?
                                 ByteOrder::Big : ByteOrder::Little); });
    }
  }
}
