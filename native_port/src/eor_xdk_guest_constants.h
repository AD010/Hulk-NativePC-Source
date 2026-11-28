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
struct OriginalRasterWindow {
  float x=0, y=0, width=0, height=0, min_depth=0, max_depth=1;
  bool scissor_enabled=false;
  std::array<int32_t,4> scissor{};
};
struct OriginalSurfaceExtent {
  uint32_t width=0,height=0;
  bool operator==(const OriginalSurfaceExtent&) const=default;
};
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
  std::optional<OriginalRasterWindow> raster;
  std::optional<OriginalSurfaceExtent> original_color_extent,original_depth_extent;
  // Separate physical Xenos TFETCH slots 16..19 (VS logical s0..s3).
  std::array<uint32_t,4> original_vertex_texture_guest_objects{};
};
// Read only the two source-proven shader object bindings for an epoch
// that is already ineligible for native replay. This is not a matrix,
// raster, material or geometry witness and cannot authorize a Draw.
struct OriginalShaderObjects {uint32_t vertex=0,pixel=0;};
struct MatrixRegisters {
  uint32_t original_first_register=0;
  uint32_t original_register_count=0;
  std::array<float,16> values{};
};
// Reads exact guest addresses into a caller-owned snapshot. The function
// never logs/publishes original game data. Reader MUST check committed guest
// memory; this function never dereferences a guessed original guest pointer.
using ReadGuestBytes=bool (*)(void*,uint32_t,std::byte*,size_t) noexcept;
inline std::optional<OriginalShaderObjects> CaptureUsaShaderObjects(
    uint32_t device,ReadGuestBytes read,void* state) noexcept {
  constexpr uint32_t offset=12692u;
  if(!device || !read || uint64_t(device)+offset+8u>(uint64_t{1}<<32))return {};
  std::array<std::byte,8> bytes{};
  if(!read(state,device+offset,bytes.data(),bytes.size()))return {};
  const auto word=[](const std::byte* p) noexcept {
    return uint32_t(std::to_integer<uint8_t>(p[0]))<<24 |
           uint32_t(std::to_integer<uint8_t>(p[1]))<<16 |
           uint32_t(std::to_integer<uint8_t>(p[2]))<<8 |
           uint32_t(std::to_integer<uint8_t>(p[3]));
  };
  return OriginalShaderObjects{word(bytes.data()+4),word(bytes.data())};
}
// Source AOT ClearF (USA guest 0x82888C40, host 0x141FEC5A0)
// reads the currently bound color or depth object at device+12440/+12456.
// At host 0x141FEC6DD..0x141FEC736 it reads BE object+36, extracts
// bits 3..17 and 18..31 and increments both extents.
inline std::optional<OriginalSurfaceExtent> ReadUsaSurfaceExtent(
    uint32_t object,ReadGuestBytes read,void* state) noexcept {
  if(!object || (object&3u) || !read || uint64_t(object)+40u>(uint64_t{1}<<32))return {};
  std::array<std::byte,4> b{};
  if(!read(state,object+36u,b.data(),b.size()))return {};
  const auto word=uint32_t(std::to_integer<uint8_t>(b[0]))<<24 |
                  uint32_t(std::to_integer<uint8_t>(b[1]))<<16 |
                  uint32_t(std::to_integer<uint8_t>(b[2]))<<8 |
                  uint32_t(std::to_integer<uint8_t>(b[3]));
  const OriginalSurfaceExtent result{((word>>3)&0x7fffu)+1u,(word>>18)+1u};
  if(result.width>8192u || result.height>8192u)return {};
  return result;
}
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
  result.original_color_extent=ReadUsaSurfaceExtent(result.original_rt0_guest_object,reader,reader_state);
  result.original_depth_extent=ReadUsaSurfaceExtent(result.original_depth_guest_object,reader,reader_state);
  for(uint32_t i=0;i<result.original_texture_guest_objects.size();++i)
    result.original_texture_guest_objects[i]=at(12544+i*4);
  for(uint32_t i=0;i<result.original_vertex_texture_guest_objects.size();++i)
    result.original_vertex_texture_guest_objects[i]=at(0x3100u+(16u+i)*4u);
  // USA GetViewport (0x8287E5E8) reads six BE floats. SetScissorRect
  // (0x8287E4C0) clips to the active viewport and consults +11856
  // before intersecting the original signed RECT at +12676.
  const auto fp=[&](uint32_t offset) { return std::bit_cast<float>(at(offset)); };
  result.raster=OriginalRasterWindow{fp(12648),fp(12652),fp(12656),fp(12660),
      fp(12664),fp(12668),at(11856)!=0,
      {std::bit_cast<int32_t>(at(12676)),std::bit_cast<int32_t>(at(12680)),
       std::bit_cast<int32_t>(at(12684)),std::bit_cast<int32_t>(at(12688))}};
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
// Snapshot the original Xbox USA XDK device's seven live fixed graphics
// register groups. They contain genuine per-Draw render state, not PM4
// commands or reconstructed Xenia state. A separate validated translation
// is still required before these can authorize a Vulkan graphics PSO.
struct OriginalXdkFixedGraphicsState {
  static constexpr std::array<uint32_t,7> kGuestOffsets{
      10368u,10444u,10528u,10548u,10596u,10680u,10832u};
  static constexpr std::array<uint32_t,7> kRegisterBases{
      0x2000u,0x2100u,0x2180u,0x2200u,0x2280u,0x2300u,0x2380u};
  static constexpr std::array<size_t,7> kCounts{16u,21u,5u,12u,21u,38u,8u};
  static constexpr size_t kTotalRegisters=121u;
  uint32_t original_guest_device=0;
  std::array<uint32_t,kTotalRegisters> original_register_values{};
  std::optional<uint32_t> ReadOriginalXenosRegister(
      uint32_t source_register) const noexcept {
    size_t base=0;
    for(size_t group=0;group<kCounts.size();++group) {
      const auto first=kRegisterBases[group];
      const auto count=static_cast<uint32_t>(kCounts[group]);
      if(source_register>=first && source_register-first<count)
        return original_register_values[base+source_register-first];
      base+=count;
    }
    return std::nullopt;
  }
};
inline std::optional<OriginalXdkFixedGraphicsState>
CaptureUsaFixedGraphicsState(uint32_t guest_device,ReadGuestBytes read,
                             void* state) noexcept {
  if(!guest_device || !read)return std::nullopt;
  OriginalXdkFixedGraphicsState snapshot{};
  snapshot.original_guest_device=guest_device;
  size_t destination=0;
  // All seven fixed-state groups occupy a single 496-byte device
  // region. Read it once; the register groups still use their exact
  // original offsets. A sparse diagnostic reader may require the old
  // per-group fallback, so do not change the original acceptance rules.
  constexpr auto first=OriginalXdkFixedGraphicsState::kGuestOffsets.front();
  constexpr auto last=OriginalXdkFixedGraphicsState::kGuestOffsets.back()+
      4u*OriginalXdkFixedGraphicsState::kCounts.back();
  std::array<std::byte,last-first> combined{};
  const bool bulk=uint64_t(guest_device)+last<=uint64_t(UINT32_MAX)+1u &&
      read(state,guest_device+first,combined.data(),combined.size());
  for(size_t group=0;group<OriginalXdkFixedGraphicsState::kCounts.size();++group) {
    const size_t count=OriginalXdkFixedGraphicsState::kCounts[group];
    const uint64_t address=uint64_t(guest_device)+
        OriginalXdkFixedGraphicsState::kGuestOffsets[group];
    if(address+count*4u>uint64_t(UINT32_MAX)+1u)return std::nullopt;
    std::array<std::byte,38*4> bytes{};
    if(!bulk && !read(state,static_cast<uint32_t>(address),bytes.data(),count*4u))
      return std::nullopt;
    const auto* source=bulk ? combined.data()+
        (OriginalXdkFixedGraphicsState::kGuestOffsets[group]-first) : bytes.data();
    for(size_t i=0;i<count;++i) {
      const auto* w=source+i*4;
      snapshot.original_register_values[destination++]=
          uint32_t(std::to_integer<uint8_t>(w[0]))<<24 |
          uint32_t(std::to_integer<uint8_t>(w[1]))<<16 |
          uint32_t(std::to_integer<uint8_t>(w[2]))<<8 |
          uint32_t(std::to_integer<uint8_t>(w[3]));
    }
  }
  return snapshot;
}
// A null XDK pixel binding is not a missing archive shader. Preserve
// vertex-only depth/stencil passes separately from unproven or color draws.
enum class OriginalPixelStageKind : uint8_t {
  Unknown, BoundProgram, VertexOnlyDepthStencil, VertexOnlyNoColor
};
inline constexpr OriginalPixelStageKind ClassifyOriginalPixelStage(
    uint32_t vertex,uint32_t pixel,uint32_t depth_object,
    uint32_t color_mask,uint32_t depth_control,uint32_t color_control) noexcept {
  if(!vertex)return OriginalPixelStageKind::Unknown;
  if(pixel)return OriginalPixelStageKind::BoundProgram;
  // RB_COLOR_MASK covers all four color targets. Alpha test and
  // alpha-to-coverage must not be silently removed with the pixel stage.
  if((color_mask&0xffffu) || (color_control&0x18u))
    return OriginalPixelStageKind::Unknown;
  if(depth_control&3u)
    return depth_object?OriginalPixelStageKind::VertexOnlyDepthStencil:
                        OriginalPixelStageKind::Unknown;
  // No attachment writes is not permission to skip queries or Draws.
  return OriginalPixelStageKind::VertexOnlyNoColor;
}
inline OriginalPixelStageKind CaptureUsaPixelStageKind(
    uint32_t device,const OriginalShaderObjects& objects,
    ReadGuestBytes read,void* state,
    std::optional<OriginalXdkFixedGraphicsState>* captured_fixed=nullptr) noexcept {
  if(captured_fixed)captured_fixed->reset();
  if(!objects.vertex)return OriginalPixelStageKind::Unknown;
  if(objects.pixel)return OriginalPixelStageKind::BoundProgram;
  if(!device || !read || uint64_t(device)+12460u>(uint64_t{1}<<32))
    return OriginalPixelStageKind::Unknown;
  std::array<std::byte,4> raw{};
  if(!read(state,device+12456u,raw.data(),raw.size()))
    return OriginalPixelStageKind::Unknown;
  const uint32_t depth_object=uint32_t(std::to_integer<uint8_t>(raw[0]))<<24 |
      uint32_t(std::to_integer<uint8_t>(raw[1]))<<16 |
      uint32_t(std::to_integer<uint8_t>(raw[2]))<<8 |
      uint32_t(std::to_integer<uint8_t>(raw[3]));
  const auto fixed=CaptureUsaFixedGraphicsState(device,read,state);
  if(!fixed)return OriginalPixelStageKind::Unknown;
  const auto mask=fixed->ReadOriginalXenosRegister(0x2104u);
  const auto depth=fixed->ReadOriginalXenosRegister(0x2200u);
  const auto color=fixed->ReadOriginalXenosRegister(0x2202u);
  if(!mask || !depth || !color)return OriginalPixelStageKind::Unknown;
  if(captured_fixed)*captured_fixed=fixed;
  return ClassifyOriginalPixelStage(objects.vertex,objects.pixel,depth_object,*mask,*depth,*color);
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
  std::array<std::array<uint32_t,6>,16> pixel_fetch{};
  uint16_t valid_pixel_fetch=0;
  bool complete_float_banks=false;
  // Live source fixed raster/depth/blend state sampled at this exact Draw.
  std::optional<OriginalXdkFixedGraphicsState> fixed_graphics_state;
  // XDK boolean constants: 128 VS bits followed by 128 PS bits.
  std::optional<std::array<uint32_t,8>> original_boolean_words;
  std::array<std::array<uint32_t,6>,4> vertex_fetch{};
  uint8_t valid_vertex_fetch=0;
};
// Original USA AOT DrawIndexedVertices 0x8288E318 uploads registers
// SQ_BOOL_CONST_0 (0x4900) from device+0x2780. The original setters
// 0x82873D28 and 0x82873D88 index words at +0x2780 and +0x2790,
// respectively, by (logical index >> 5), and set (index & 31).
inline void CaptureUsaBooleanRegisters(uint32_t device,ReadGuestBytes read,void* state,
                                       OriginalShaderRegisters& registers) noexcept {
  registers.original_boolean_words.reset();
  constexpr uint32_t offset=0x2780u;
  std::array<std::byte,32> bytes{};
  if(!device || !read || uint64_t(device)+offset+bytes.size()>(uint64_t{1}<<32) ||
     !read(state,device+offset,bytes.data(),bytes.size()))return;
  std::array<uint32_t,8> result{};
  for(size_t i=0;i<result.size();++i) {
    const auto* p=bytes.data()+i*4;
    result[i]=(uint32_t(std::to_integer<uint8_t>(p[0]))<<24) |
              (uint32_t(std::to_integer<uint8_t>(p[1]))<<16) |
              (uint32_t(std::to_integer<uint8_t>(p[2]))<<8) |
              uint32_t(std::to_integer<uint8_t>(p[3]));
  }
  registers.original_boolean_words=result;
}
// Pinned XenosRecomp scalar ABI defines each CTAB bool as 1 << index
// for VS and 1 << (index + 16) for PS. The complete original words are
// retained; only the represented logical 16 bits of each stage are packed.
inline std::optional<uint32_t> PackOriginalScalarBooleans(
    const OriginalShaderRegisters& registers) noexcept {
  if(!registers.original_boolean_words)return {};
  return ((*registers.original_boolean_words)[0]&0xffffu) |
         (((*registers.original_boolean_words)[4]&0xffffu)<<16);
}
inline void CaptureUsaPixelFetches(uint32_t device,ReadGuestBytes read,void* state,
                                  OriginalShaderRegisters& registers) noexcept {
  registers.valid_pixel_fetch=0;
  constexpr uint32_t start=48u*24u; // USA 82675EA0 and 82675E00, s0..s15
  std::array<std::byte,16*24> bytes{};
  if(!device || !read || uint64_t(device)+start+bytes.size()>(uint64_t{1}<<32) ||
     !read(state,device+start,bytes.data(),bytes.size()))return;
  for(unsigned slot=0;slot<16;++slot) {
    auto& fetch=registers.pixel_fetch[slot];
    for(unsigned i=0;i<6;++i) {
      const auto* p=bytes.data()+slot*24+i*4;
      fetch[i]=uint32_t(std::to_integer<uint8_t>(p[0]))<<24 |
               uint32_t(std::to_integer<uint8_t>(p[1]))<<16 |
               uint32_t(std::to_integer<uint8_t>(p[2]))<<8 |
               uint32_t(std::to_integer<uint8_t>(p[3]));
    }
    if((fetch[0]&3u)==2u && ((fetch[1]|fetch[5])>>12&0x1ffffu))
      registers.valid_pixel_fetch|=static_cast<uint16_t>(1u<<slot);
  }
}
// USA XDK 6534: 8287E0A0/8287E120 index the bound texture object at
// device+0x3100+4*slot and the fetch at device+0x480+24*slot.
// Original VS TFETCH constants 16..19 are distinct from PS 0..15.
inline void CaptureUsaVertexFetches(uint32_t device,ReadGuestBytes read,void* state,
                                   OriginalShaderRegisters& registers) noexcept {
  registers.valid_vertex_fetch=0;
  registers.vertex_fetch={};
  constexpr uint32_t start=0x480u+16u*24u;
  std::array<std::byte,4*24> bytes{};
  if(!device || !read || uint64_t(device)+start+bytes.size()>(uint64_t{1}<<32) ||
     !read(state,device+start,bytes.data(),bytes.size()))return;
  for(unsigned slot=0;slot<4;++slot) {
    auto& fetch=registers.vertex_fetch[slot];
    for(unsigned i=0;i<6;++i) {
      const auto* p=bytes.data()+slot*24+i*4;
      fetch[i]=uint32_t(std::to_integer<uint8_t>(p[0]))<<24 |
               uint32_t(std::to_integer<uint8_t>(p[1]))<<16 |
               uint32_t(std::to_integer<uint8_t>(p[2]))<<8 |
               uint32_t(std::to_integer<uint8_t>(p[3]));
    }
    if((fetch[0]&3u)==2u && ((fetch[1]|fetch[5])>>12&0x1ffffu))
      registers.valid_vertex_fetch|=static_cast<uint8_t>(1u<<slot);
  }
}
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
  static_assert(kPsFloatOffset==kVsFloatOffset+kBytes);
  // The original two banks are contiguous. One checked read avoids
  // redundant Windows region queries on every admissible Draw.
  if(!reader(reader_state,static_cast<uint32_t>(vs_at),raw.data(),raw.size()) &&
     (!reader(reader_state,static_cast<uint32_t>(vs_at),raw.data(),kBytes) ||
      !reader(reader_state,static_cast<uint32_t>(ps_at),raw.data()+kBytes,kBytes)))
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
  candidate.complete_float_banks=true;
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
