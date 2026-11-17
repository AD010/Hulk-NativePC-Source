// Exact original Hulk USA Xbox360 XDK device matrix capture.
// These original registers belong to the GAME's VS constants, not a guessed
// camera object, SPIR-V UBO layout, or Vulkan render target.
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace hulk::eor::xdk {
struct OriginalVsMatrices {
  static constexpr uint32_t kFirstRegister=7;
  static constexpr uint32_t kLastRegister=25;
  static constexpr size_t kRegisterCount=kLastRegister-kFirstRegister+1;
  uint32_t original_guest_device=0;
  uint32_t original_vs_guest_object=0;
  uint32_t original_ps_guest_object=0;
  // Verified original XDK 6534 device fields. Guest object pointers are
  // provenance inputs, NEVER host Vulkan handles or material hashes.
  uint32_t original_vertex_declaration_guest_object=0;
  uint32_t original_index_buffer_guest_object=0;
  uint32_t original_rt0_guest_object=0;
  uint32_t original_depth_guest_object=0;
  std::array<uint32_t,16> original_texture_guest_objects{};
  std::array<float,kRegisterCount*4> c7_through_c25{};
  uint32_t finite_register_mask=0;
};
struct MatrixRegisters {
  uint32_t original_first_register=0;
  uint32_t original_register_count=0;
  std::array<float,16> values{};
};
// Reads exact guest addresses into a caller-owned snapshot. The function
// never logs/publishes original game data. Reader MUST check committed guest
// memory; this function never dereferences a guessed original guest pointer.
using ReadGuestBytes=bool (*)(void*,uint32_t,std::byte*,size_t) noexcept;
inline std::optional<OriginalVsMatrices> CaptureUsaVsMatrices(
    uint32_t guest_device,ReadGuestBytes reader,void* reader_state) noexcept {
  if(!guest_device || !reader)return std::nullopt;
  constexpr uint32_t kVsFloats=1920;  // verified USA XDK device layout
  constexpr uint32_t kFirstObject=11820; // original vertex declaration
  constexpr uint32_t kLastObject=12696;  // original vertex shader
  constexpr uint32_t kObjectBytes=kLastObject-kFirstObject+4;
  constexpr uint32_t kBytes=uint32_t(OriginalVsMatrices::kRegisterCount*16);
  const uint64_t matrices_at=uint64_t(guest_device)+kVsFloats+
      16u*OriginalVsMatrices::kFirstRegister;
  const uint64_t objects_at=uint64_t(guest_device)+kFirstObject;
  if(matrices_at+kBytes>uint64_t(UINT32_MAX)+1 ||
     objects_at+kObjectBytes>uint64_t(UINT32_MAX)+1)
    return std::nullopt;
  std::array<std::byte,kBytes> raw{};
  std::array<std::byte,kObjectBytes> objects{};
  if(!reader(reader_state,uint32_t(matrices_at),raw.data(),raw.size()) ||
     !reader(reader_state,uint32_t(objects_at),objects.data(),objects.size()))
    return std::nullopt;
  const auto word=[](const std::byte* p) noexcept {
    return (uint32_t(std::to_integer<uint8_t>(p[0]))<<24) |
           (uint32_t(std::to_integer<uint8_t>(p[1]))<<16) |
           (uint32_t(std::to_integer<uint8_t>(p[2]))<<8) |
            uint32_t(std::to_integer<uint8_t>(p[3]));
  };
  OriginalVsMatrices result{};
  result.original_guest_device=guest_device;
  const auto at=[&](uint32_t offset) noexcept {
    return word(objects.data()+offset-kFirstObject);
  };
  result.original_vs_guest_object=at(12696);
  result.original_ps_guest_object=at(12692);
  result.original_vertex_declaration_guest_object=at(11820);
  result.original_index_buffer_guest_object=at(12436);
  result.original_rt0_guest_object=at(12440);
  result.original_depth_guest_object=at(12456);
  for(uint32_t i=0;i<result.original_texture_guest_objects.size();++i)
    result.original_texture_guest_objects[i]=at(12544+i*4);
  for(size_t reg=0;reg<OriginalVsMatrices::kRegisterCount;++reg) {
    bool finite=true;
    for(size_t component=0;component<4;++component) {
      const auto f=std::bit_cast<float>(word(raw.data()+reg*16+component*4));
      result.c7_through_c25[reg*4+component]=f;
      finite=finite && std::isfinite(f);
    }
    if(finite)result.finite_register_mask|=1u<<reg;
  }
  return result;
}
// Source reflection establishes exact VS registers (typically c7 World,
// c11 ViewProj, c21 PrevWorld, c22 SkyViewProj). This helper does not
// invent a fourth row when World declares only THREE float4 registers.
inline std::optional<MatrixRegisters> ReadOriginalMatrix(
    const OriginalVsMatrices& snapshot,uint32_t first,uint32_t count) noexcept {
  if((count!=3 && count!=4) ||
     first<OriginalVsMatrices::kFirstRegister ||
     first>OriginalVsMatrices::kLastRegister ||
     count>OriginalVsMatrices::kLastRegister-first+1)
    return std::nullopt;
  MatrixRegisters result{};
  result.original_first_register=first;
  result.original_register_count=count;
  for(uint32_t reg=0;reg<count;++reg) {
    const auto offset=first-OriginalVsMatrices::kFirstRegister+reg;
    if(!(snapshot.finite_register_mask & (1u<<offset)))
      return std::nullopt;
    for(size_t component=0;component<4;++component)
      result.values[reg*4+component]=
          snapshot.c7_through_c25[offset*4+component];
  }
  return result;
}
} // namespace hulk::eor::xdk
