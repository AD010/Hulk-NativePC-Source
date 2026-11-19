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
#include <utility>

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
  // Real USA XDK 6534 SetStreamSource slot 0..15 device shadow (+12460).
  // Guest object identity only: NOT a model hash or Vulkan buffer handle.
  std::array<uint32_t,16> original_vertex_buffer_guest_objects{};
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
  for(uint32_t i=0;i<result.original_vertex_buffer_guest_objects.size();++i)
    result.original_vertex_buffer_guest_objects[i]=at(12460+i*4);
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
// Entire original USA XDK 2.0.6534 shader-float banks, snapshotted
// at the SAME PPC Draw as original resource identities. The original device
// shadow layout has 256 float4 VS registers at +1920 and 256 float4 PS
// registers at +6016; restricting all future native material constants to
// VS c7..c25 would silently lose pixel lighting, fog and effect parameters.
// This is an INPUT capture, NOT a guessed Vulkan constant-buffer layout.
enum class OriginalShaderStage : uint8_t { Vertex=1, Pixel=2 };
struct OriginalShaderRegisters {
  static constexpr uint32_t kRegisterCount=256;
  std::array<float,kRegisterCount*4> vertex{};
  std::array<float,kRegisterCount*4> pixel{};
  std::array<uint64_t,4> finite_vertex{},finite_pixel{};
};
inline bool CaptureUsaAllShaderRegisters(
    uint32_t guest_device,ReadGuestBytes reader,void* reader_state,
    OriginalShaderRegisters& destination) noexcept {
  if(!guest_device || !reader)return false;
  constexpr uint32_t kVsFloatOffset=1920;
  constexpr uint32_t kPsFloatOffset=6016;
  constexpr uint32_t kBytes=OriginalShaderRegisters::kRegisterCount*16;
  const uint64_t vs_at=uint64_t(guest_device)+kVsFloatOffset;
  const uint64_t ps_at=uint64_t(guest_device)+kPsFloatOffset;
  if(vs_at+kBytes>uint64_t(UINT32_MAX)+1 ||
     ps_at+kBytes>uint64_t(UINT32_MAX)+1)return false;
  std::array<std::byte,2*kBytes> raw{};
  if(!reader(reader_state,static_cast<uint32_t>(vs_at),
             raw.data(),kBytes) ||
     !reader(reader_state,static_cast<uint32_t>(ps_at),
             raw.data()+kBytes,kBytes))
    return false;
  const auto be32=[](const std::byte* p) noexcept -> uint32_t {
    return (uint32_t(std::to_integer<uint8_t>(p[0]))<<24) |
           (uint32_t(std::to_integer<uint8_t>(p[1]))<<16) |
           (uint32_t(std::to_integer<uint8_t>(p[2]))<<8) |
            uint32_t(std::to_integer<uint8_t>(p[3]));
  };
  // There is no reader failure beyond this point: publish both complete
  // stage banks together, not a VS update with a stale PS bank.
  OriginalShaderRegisters candidate{};
  for(uint32_t i=0;i<OriginalShaderRegisters::kRegisterCount;++i) {
    bool vs_finite=true,ps_finite=true;
    for(uint32_t component=0;component<4;++component) {
      const uint32_t at=16*i+4*component;
      const float vs=std::bit_cast<float>(be32(raw.data()+at));
      const float ps=std::bit_cast<float>(be32(raw.data()+kBytes+at));
      candidate.vertex[i*4+component]=vs;
      candidate.pixel[i*4+component]=ps;
      vs_finite=vs_finite && std::isfinite(vs);
      ps_finite=ps_finite && std::isfinite(ps);
    }
    if(vs_finite)candidate.finite_vertex[i/64]|=uint64_t(1)<<(i%64);
    if(ps_finite)candidate.finite_pixel[i/64]|=uint64_t(1)<<(i%64);
  }
  destination=std::move(candidate);
  return true;
}
inline std::optional<std::array<float,4>> ReadOriginalShaderFloat4(
    const OriginalShaderRegisters& captured,OriginalShaderStage stage,
    uint32_t index) noexcept {
  if(index>=OriginalShaderRegisters::kRegisterCount ||
     (stage!=OriginalShaderStage::Vertex &&
      stage!=OriginalShaderStage::Pixel))return std::nullopt;
  const auto& mask=stage==OriginalShaderStage::Vertex ?
      captured.finite_vertex:captured.finite_pixel;
  if((mask[index/64] & (uint64_t(1)<<(index%64)))==0)
    return std::nullopt;
  const auto& values=stage==OriginalShaderStage::Vertex ?
      captured.vertex:captured.pixel;
  return std::array<float,4>{values[index*4],values[index*4+1],
                             values[index*4+2],values[index*4+3]};
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
