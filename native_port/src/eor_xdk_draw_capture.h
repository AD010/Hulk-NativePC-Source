// Actual USA XDK call provenance, NOT a fabricated EoR Draw submission.
// Keep original PPC call-through and memory untouched. Opt-in audit only.
#pragma once
#include <compare>
#include "eor_xdk_guest_constants.h"
#include "eor_usa_material_textures.h"
#include "eor_original_resolve_region.h"
#include "eor_original_virtual_shader_identity.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <tuple>

namespace hulk::eor::xdk {
enum class Kind : uint8_t { DrawVertices, DrawIndexedVertices, DrawVerticesUP, EndVertices };
// These six bl instructions were observed in the PRIVATE AOT corpus from
// SHA-pinned USA default.xex (98,726 generated PPC function definitions).
// r3..r6 / r3..r7 are independently verified source XDK method arguments.
// A direct bl sets LR to its next instruction; a TAIL branch (b) does not.
// Never resolve a tail-call LR to a presumed engine Draw owner, and never
// claim that knowing a caller proves its model, material, effect or shader.
inline constexpr uint32_t ProvenUsaXdkDirectBlCaller(
    uint32_t xdk_function,uint32_t source_lr) noexcept {
  switch(xdk_function) {
    case 0x8288D8F8u: return source_lr==0x82676344u ? 0x82676328u : 0u;
    case 0x8288DF30u: // XDK DrawVertices: only one source-defined bl.
      return source_lr==0x82676430u ? 0x82676400u : 0u;
    case 0x8288E318u: // XDK DrawIndexedVertices: five source-defined bl.
      switch(source_lr) {
        case 0x82640614u: return 0x82640578u;
        case 0x8264164Cu: return 0x82641618u;
        case 0x8265B53Cu: return 0x8265B420u;
        case 0x82668184u: return 0x826680B8u;
        case 0x8266899Cu: return 0x826688D0u;
        default: return 0u;
      }
    default: return 0u;
  }
}
// Original actual XDK command chronology is essential for a sole Vulkan
// presenter: Draws alone omit clears, RT/depth changes, tiled resolves and
// postprocessing. The events record real PPC call arguments but intentionally
// do NOT invent the ABI of pointed-to guest rectangle/resolve structures.
enum class CommandKind : uint8_t {
  DrawVertices, DrawIndexedVertices, DrawVerticesUP,
  SetRenderTarget, SetDepthStencilSurface,
  ClearF, Resolve, BeginTiling, EndTiling, Present, EndVertices
};
// Source-verified ClearF(dev, flags, rect, color*, Z in f1, stencil).
// Values are BE-decoded at the REAL PPC call, before guest memory can
// change. Original bound surfaces still require independent ownership proof.
struct OriginalXdkClearF {
  uint32_t flags=0;
  bool has_rect=false,has_color=false;
  std::array<int32_t,4> rect{}; // original x0,y0,x1,y1
  std::array<float,4> color{};
  float depth=1.f;
  uint8_t stencil=0;
};
inline bool IsValidOriginalTargetClear(const OriginalXdkClearF& clear,
                                       uint32_t width,uint32_t height) noexcept {
  if(!width || !height || width>INT32_MAX || height>INT32_MAX ||
     !clear.flags || (clear.flags&~0x31u) ||
     ((clear.flags&1u) && !clear.has_color) ||
     ((clear.flags&0x10u) &&
      (!std::isfinite(clear.depth) || clear.depth<0.f || clear.depth>1.f)))
    return false;
  if(clear.flags&1u)
    for(float channel:clear.color)
      if(!std::isfinite(channel))return false;
  const auto& r=clear.rect;
  return !clear.has_rect || (r[0]>=0 && r[1]>=0 && r[2]>r[0] && r[3]>r[1] &&
                             r[2]<=int64_t(width) && r[3]<=int64_t(height));
}
inline bool IsOriginalFullTargetClear(const OriginalXdkClearF& clear,
                                      uint32_t width,uint32_t height) noexcept {
  return IsValidOriginalTargetClear(clear,width,height) &&
         (!clear.has_rect || clear.rect==std::array<int32_t,4>{0,0,int32_t(width),int32_t(height)});
}
std::optional<OriginalXdkClearF> CaptureOriginalXdkClearF(
    const std::array<uint32_t,8>& gpr,double depth_f1,
    ReadGuestBytes reader,void* guest_base) noexcept;
struct RawCommand {
  CommandKind kind=CommandKind::DrawVertices;
  uint32_t function=0, lr=0;
  std::array<uint32_t,8> gpr{};  // original r3..r10
  uint32_t draw_ordinal=UINT32_MAX; // populated only for Draw commands
  std::optional<OriginalXdkClearF> captured_clear;
  std::optional<OriginalResolveRegion> captured_resolve;
  std::optional<OriginalRasterWindow> captured_resolve_raster;
  uint32_t captured_resolve_color_owner=0;
  // Clear-only passes may have no Draw from which to learn their surface.
  // Snapshot the real bound device and RB format state at ClearF entry.
  // Only ClearF owns these snapshots. Embedding them in every Draw,
  // Resolve and Present inflated the entire command stream by more than
  // a kilobyte per command; immutable shared storage keeps copies cheap.
  std::shared_ptr<const OriginalVsMatrices> captured_clear_targets;
  std::shared_ptr<const OriginalXdkFixedGraphicsState> captured_clear_fixed;
};
std::optional<OriginalResolveRegion> CaptureOriginalColorResolveRegion(
    const std::array<uint32_t,8>& gpr,ReadGuestBytes read,void* state) noexcept;
inline std::optional<OriginalResolveRegion> DecodeOriginalColorResolveRegion(
    const RawCommand& cmd,uint32_t width,uint32_t height) noexcept {
  if(cmd.kind!=CommandKind::Resolve || cmd.gpr[1] || !cmd.gpr[3] ||
     cmd.gpr[5] || cmd.gpr[6] || !width || !height || width>8192 || height>8192)return {};
  if(!cmd.gpr[2] && !cmd.gpr[4] && !cmd.captured_resolve)
    return OriginalResolveRegion{{0,0,int32_t(width),int32_t(height)},{0,0}};
  if(!cmd.captured_resolve)return {};
  auto region=*cmd.captured_resolve;
  if(!cmd.gpr[2])region.source={0,0,int32_t(width),int32_t(height)};
  if(!cmd.gpr[4])region.destination={0,0};
  if(!ValidOriginalResolveRegion(region,width,height,width,height))return {};
  return region;
}
struct OriginalGuestBufferBytes {
  uint32_t guest_object=0,guest_address=0,guest_size=0;
  std::vector<std::byte> bytes;
};
struct UsaIndexBufferBinding {
  uint32_t object=0,address=0,bytes=0,element_bytes=0,endian=0;
  bool Matches(const OriginalGuestBufferBytes& buffer) const noexcept {
    return object && object==buffer.guest_object && address==buffer.guest_address &&
           bytes==buffer.guest_size && bytes==buffer.bytes.size() &&
           (element_bytes==2 || element_bytes==4) && address%element_bytes==0 &&
           !(bytes%element_bytes) && endian<4;
  }
  // From least to most significant byte, the four DMA modes use these
  // permutations within a physical DWORD. 16-bit pairs can exchange lanes.
  uint32_t ByteXor() const noexcept {return endian^(endian>>1);}
  std::optional<uint32_t> Read(std::span<const std::byte> raw,uint32_t index) const noexcept;
};
std::optional<UsaIndexBufferBinding> ReadUsaIndexBufferBinding(
    uint32_t object,ReadGuestBytes read,void* state) noexcept;
struct UsaVertexElement {
  uint16_t stream=0,byte_offset=0;
  uint32_t original_packed_type=0,original_packed_usage=0;
  auto operator<=>(const UsaVertexElement&) const = default;
};
struct UsaVertexDeclaration {
  uint32_t object=0;
  uint16_t streams=0;
  bool font_main_elements=false;
  // The complete original XDK declaration, not merely a mask of streams.
  // A source EoR model's 4-byte mesh semantics can be compared with the
  // actual live guest element offsets before native vertex buffer binding.
  std::vector<UsaVertexElement> elements;
};
std::optional<UsaVertexDeclaration> ReadUsaVertexDeclaration(
    uint32_t object,ReadGuestBytes read,void* state) noexcept;
struct UsaVertexStreamBinding {
  uint32_t object=0,allocation_address=0,allocation_bytes=0;
  uint32_t offset=0,length=0,stride=0;
  bool Matches(const OriginalGuestBufferBytes& buffer) const noexcept {
    return object && object==buffer.guest_object && allocation_address==buffer.guest_address &&
           allocation_bytes==buffer.guest_size && buffer.bytes.size()==allocation_bytes &&
           offset<=allocation_bytes && length==allocation_bytes-offset && length && stride;
  }
};
// USA SetStreamSource stores an encoded vertex fetch pair. Recover the
// current byte window from the resource and the same device's bound slot.
std::optional<UsaVertexStreamBinding> ReadUsaVertexStreamBinding(
    uint32_t device,uint32_t slot,uint32_t object,ReadGuestBytes read,void* state) noexcept;
// Per-Present immutable snapshots. Re-read at EVERY Draw: object/address
// equality does not prove unchanged vertices. Only byte equality permits
// reuse. Caller serializes Capture/Reset with the original frame collector.
class OriginalGuestBufferSnapshots {
 public:
  explicit OriginalGuestBufferSnapshots(size_t budget=64u<<20)
      : budget_(budget) {}
  std::shared_ptr<const OriginalGuestBufferBytes> Capture(
      uint32_t object,uint32_t address,uint32_t bytes,
      ReadGuestBytes reader,void* reader_state);
  void Reset() noexcept;
  size_t RetainedBytes() const noexcept {return retained_bytes_;}
 private:
  size_t budget_=0,retained_bytes_=0;
  std::map<std::tuple<uint32_t,uint32_t,uint32_t>,
           std::shared_ptr<const OriginalGuestBufferBytes>> latest_;
  std::vector<std::byte> scratch_;
};
// Original USA shader/geometry class virtual method slot +16, identified
// from the USER-PRIVATE fully mapped PPC image and its named .rdata vtables.
// This is a genuine original EoR ENGINE entrypoint, not an XDK wrapper.
// At a live original PPC method entrance, r3 contains the shader instance
// whose first BE32 word MUST equal this original class vtable. A wrong
// object/version fails closed. Method entry alone cannot prove the shader
// effect/mesh/material/texture/sampler ownership of an actual XDK Draw.
// All 52 named original USA EoR shader class vtables have just 20 distinct
// source code method entries at virtual slot +16. 33 named classes share
// 0x826432F0: never attribute a class by guest method address alone.
// The paired guest vtable MUST be read live from original BE32 this[0].
inline constexpr bool ProvenUsaEorShaderMethodEntry(uint32_t method) noexcept {
  switch(method) {
    case 0x826432f0u:
    case 0x82651978u:
    case 0x82652420u:
    case 0x82652b60u:
    case 0x8265b420u:
    case 0x82662780u:
    case 0x82665288u:
    case 0x82665500u:
    case 0x82668d50u:
    case 0x82669630u:
    case 0x8266a0c0u:
    case 0x8266c668u:
    case 0x8266cc28u:
    case 0x8266d6d0u:
    case 0x8266ddd0u:
    case 0x8266e520u:
    case 0x8266eb88u:
    case 0x8266f198u:
    case 0x8266fef8u:
    case 0x82670738u:
      return true;
    default: return false;
  }
}
inline constexpr bool ProvenUsaEorShaderMethodVtablePair(
    uint32_t method,uint32_t vtable) noexcept {
  if(!method || !vtable)return false;
  switch(method) {
    case 0x826432f0u: // 33 shared genuine EoR classes
      switch(vtable) {
        case 0x82065e78u: // ESkyDomeDynamicShader
        case 0x82065f20u: // EVolumetricCloudsShader
        case 0x82065f84u: // EVolumetricFogBoxShader
        case 0x82066020u: // EIntersectionSunRayShader
        case 0x82066074u: // EVolumePointLightOutsideShader
        case 0x820660e4u: // EVolumePointLightInsideShader
        case 0x82066154u: // EVolumeSpotLightOutsideShader
        case 0x820661c4u: // EVolumeSpotLightInsideShader
        case 0x82066234u: // EGroundFogPlaneShader
        case 0x82066294u: // EBuildingWithWindowsShader
        case 0x820662fcu: // EBuildingWithWindowsShader2UV
        case 0x8206636cu: // ESkyDomeShader
        case 0x820663f4u: // ERipplePuddleShader
        case 0x82066450u: // EBuildingRoofShader
        case 0x8206659cu: // EOverheadCloudsShader
        case 0x820665fcu: // ESurfaceEmitterShader
        case 0x8206665cu: // ESurfaceDripShader
        case 0x820666b4u: // ELeafClusterShader
        case 0x82066744u: // EExplosionDistortionShader
        case 0x820667acu: // ECloudLayerShader
        case 0x82066804u: // EEnergyShieldShader
        case 0x82066860u: // EOceanFastShader
        case 0x820668f0u: // ECameraParticleBoxShader
        case 0x8206b550u: // system_white
        case 0x8206ee6cu: // pingPong
        case 0x8206eee4u: // EEmissiveTranslucentUVAnimShader
        case 0x8206ef5cu: // EEmissiveTranslucentUVScrollShader
        case 0x8206eff8u: // ESkinnedMultiMatEnvPartialEmissiveScrollShader
        case 0x8206f0acu: // EHairShader
        case 0x8206f13cu: // ETransparencyCharacterShader
        case 0x8206f1acu: // ETransparencyShader
        case 0x8206f20cu: // ESkinMorphShader
        case 0x8206f530u: // EDistortionShader
          return true;
        default: return false;
      }
    case 0x82651978u: return vtable==0x8206d5a0u; // Debug
    case 0x82652420u: return vtable==0x8206d64cu; // ELightConeShader
    case 0x82652b60u: return vtable==0x8206d760u; // Ribbon
    case 0x8265b420u: return vtable==0x82066504u; // Decal
    case 0x82662780u: return vtable==0x82065ec8u; // ECRIMovieShader
    case 0x82665288u: return vtable==0x820664acu; // ELensFlareShader
    case 0x82665500u: return vtable==0x82066544u; // ECollisionShader
    case 0x82668d50u: return vtable==0x8206ebfcu; // TexturedRect
    case 0x82669630u: return vtable==0x8206ecacu; // EScreenRectShader
    case 0x8266a0c0u: return vtable==0x8206ed6cu; // EFontShader
    case 0x8266c668u: return vtable==0x8206f4e8u; // FillRect
    case 0x8266cc28u: return vtable==0x8206f5dcu; // ESkyShader
    case 0x8266d6d0u: return vtable==0x8206f710u; // ERopeShader
    case 0x8266ddd0u: return vtable==0x8206f7ccu; // EFlatShadowShader
    case 0x8266e520u: return vtable==0x8206f8a8u; // ERippleGenShader
    case 0x8266eb88u: return vtable==0x8206f994u; // ESSAOShader
    case 0x8266f198u: return vtable==0x8206fa6cu; // ELaserSightShader
    case 0x8266fef8u: return vtable==0x8206fb2cu; // EGPURainShader
    case 0x82670738u: return vtable==0x8206fce4u; // EGroundFogGlobalShader
    default: return false;
  }
}
// This convenience helper only returns a UNIQUE method owner. For
// shared EoR methods it deliberately returns 0 (ambiguous class).
// Original USA EoR 0x82642A88 dispatches the per-render-item
// virtual method at original class vtable slot +44 (index 11).
// This second source hook retains EXACT shader instance and render
// item pointer for EVERY one of the 52 named shader classes.
// 27 classes share method 0x82642370: vtable identity is required.
// Source guest LR from the real indirect bctrl 0x82642BD4 is
// exactly 0x82642BD8. Never treat a direct unrelated invocation
// as an original EoR source item.
inline constexpr bool ProvenUsaEorRenderItemMethodVtablePair(
    uint32_t method,uint32_t vtable) noexcept {
  switch(method) {
    case 0x8263dcd0u:
      switch(vtable) {
        case 0x82066294u:
        case 0x820662fcu:
          return true;
        default: return false;
      }
    case 0x82642370u:
      switch(vtable) {
        case 0x82065ec8u:
        case 0x82065f20u:
        case 0x82066234u:
        case 0x82066504u:
        case 0x82066544u:
        case 0x8206665cu:
        case 0x820666b4u:
        case 0x8206b550u:
        case 0x8206d5a0u:
        case 0x8206d64cu:
        case 0x8206d760u:
        case 0x8206ebfcu:
        case 0x8206ecacu:
        case 0x8206ed6cu:
        case 0x8206ee6cu:
        case 0x8206eee4u:
        case 0x8206ef5cu:
        case 0x8206eff8u:
        case 0x8206f4e8u:
        case 0x8206f5dcu:
        case 0x8206f710u:
        case 0x8206f7ccu:
        case 0x8206f8a8u:
        case 0x8206f994u:
        case 0x8206fa6cu:
        case 0x8206fb2cu:
        case 0x8206fce4u:
          return true;
        default: return false;
      }
    case 0x82662108u: return vtable==0x82065e78u;
    case 0x82663270u: return vtable==0x82065f84u;
    case 0x82663aa8u: return vtable==0x82066020u;
    case 0x82663ef8u:
      switch(vtable) {
        case 0x82066074u:
        case 0x820660e4u:
          return true;
        default: return false;
      }
    case 0x82664338u:
      switch(vtable) {
        case 0x82066154u:
        case 0x820661c4u:
          return true;
        default: return false;
      }
    case 0x82664630u: return vtable==0x8206636cu;
    case 0x82664ee0u: return vtable==0x820663f4u;
    case 0x82664f88u: return vtable==0x82066450u;
    case 0x82665160u: return vtable==0x820664acu;
    case 0x826656c8u: return vtable==0x8206659cu;
    case 0x82665988u: return vtable==0x820665fcu;
    case 0x82666090u: return vtable==0x82066744u;
    case 0x826664f8u: return vtable==0x820667acu;
    case 0x82666808u: return vtable==0x82066804u;
    case 0x82666c70u: return vtable==0x82066860u;
    case 0x826672d0u: return vtable==0x820668f0u;
    case 0x8266b218u: return vtable==0x8206f0acu;
    case 0x8266b588u:
      switch(vtable) {
        case 0x8206f13cu:
        case 0x8206f1acu:
          return true;
        default: return false;
      }
    case 0x8266b8b8u: return vtable==0x8206f20cu;
    case 0x8266c8d0u: return vtable==0x8206f530u;
    default: return false;
  }
}
inline constexpr bool ProvenUsaEorRenderItemMethodEntry(uint32_t method) noexcept {
  switch(method) {
    case 0x8263dcd0u:
    case 0x82642370u:
    case 0x82662108u:
    case 0x82663270u:
    case 0x82663aa8u:
    case 0x82663ef8u:
    case 0x82664338u:
    case 0x82664630u:
    case 0x82664ee0u:
    case 0x82664f88u:
    case 0x82665160u:
    case 0x826656c8u:
    case 0x82665988u:
    case 0x82666090u:
    case 0x826664f8u:
    case 0x82666808u:
    case 0x82666c70u:
    case 0x826672d0u:
    case 0x8266b218u:
    case 0x8266b588u:
    case 0x8266b8b8u:
    case 0x8266c8d0u:
      return true;
    default: return false;
  }
}
inline constexpr uint32_t ProvenUsaEorShaderVtableForMethod(
    uint32_t method) noexcept {
  switch(method) {
    case 0x82651978u: return 0x8206d5a0u; // Debug
    case 0x82652420u: return 0x8206d64cu; // ELightConeShader
    case 0x82652b60u: return 0x8206d760u; // Ribbon
    case 0x8265b420u: return 0x82066504u; // Decal
    case 0x82662780u: return 0x82065ec8u; // ECRIMovieShader
    case 0x82665288u: return 0x820664acu; // ELensFlareShader
    case 0x82665500u: return 0x82066544u; // ECollisionShader
    case 0x82668d50u: return 0x8206ebfcu; // TexturedRect
    case 0x82669630u: return 0x8206ecacu; // EScreenRectShader
    case 0x8266a0c0u: return 0x8206ed6cu; // EFontShader
    case 0x8266c668u: return 0x8206f4e8u; // FillRect
    case 0x8266cc28u: return 0x8206f5dcu; // ESkyShader
    case 0x8266d6d0u: return 0x8206f710u; // ERopeShader
    case 0x8266ddd0u: return 0x8206f7ccu; // EFlatShadowShader
    case 0x8266e520u: return 0x8206f8a8u; // ERippleGenShader
    case 0x8266eb88u: return 0x8206f994u; // ESSAOShader
    case 0x8266f198u: return 0x8206fa6cu; // ELaserSightShader
    case 0x8266fef8u: return 0x8206fb2cu; // EGPURainShader
    case 0x82670738u: return 0x8206fce4u; // EGroundFogGlobalShader
    default: return 0u;
  }
}
// Read-only original EoR engine invocation captured BEFORE the method
// executes, and bound to any inner XDK Draw on the SAME host PPC thread.
// All source arguments remain uninterpreted until engine ABI is proven.
struct OriginalEorShaderMethodAtDraw {
  uint32_t original_shader_method_guest=0;
  uint32_t original_shader_instance_guest=0;
  uint32_t original_shader_vtable_guest=0;
  std::array<uint32_t,4> source_r4_through_r7{};
  uint8_t nesting_depth=0;
  bool original_shader_method_vtable_verified=false;
  // Only for the actual USA Decal source function 0x8265B420 when its
  // direct bl at 0x8265B538 reaches XDK DrawIndexedVertices.
  // Original PPC r28 points to the per-Draw source item; the original
  // geometry pointer is BE32(item+0x3C). The same original function binds
  // guest VB descriptor (geometry+0x18) and IB (geometry+0x38).
  // These are original ENGINE guest pointers, not model names, host
  // Vulkan IDs, a material effect pass or a guessed surface ordinal.
  uint32_t original_decal_render_item_guest=0;
  uint32_t original_decal_geometry_guest=0;
  bool original_decal_xdk_vb_ib_binding_verified=false;
  // The original Decal source method calls material binder 0x82642C68;
  // the original render item's virtual method slot+48 returns an
  // original source material descriptor at PPC LR=0x82642CB4.
  // The native 0x825F8A50 setter reads descriptor+0 and +8 and then
  // tail-calls the actual render-state binder. This records EXACT
  // guest ownership only, never equates the descriptor to an EoR
  // ShaderEffects effect hash, shader pair, sampler or host PSO.
  uint32_t original_decal_material_descriptor_guest=0;
  uint32_t original_decal_material_context_guest=0;
  uint32_t original_decal_material_word0=0;
  uint32_t original_decal_material_word8=0;
  bool original_decal_item_material_binding_verified=false;
  // Original 0x825F84C0 generic EoR state-slot binder. At source entry
  // r3 is the original material context, r4 is 1-based pass STATE
  // index (the original PDB/ShaderEffects ordinal remains unverified).
  // Original source pass record stride=92 bytes, VS/PS table indexes
  // at pass+84,+85; original source shader handle tables at material
  // +124 (VS) and +136 (PS), eight bytes per handle.
  // Exact original SetVertexShader / SetPixelShader device shadow
  // identities must agree at a subsequent SAME-thread XDK Draw.
  uint32_t original_pass_material_context_guest=0;
  uint32_t original_pass_state_one_based=0;
  uint32_t original_pass_vertex_handle_entry_guest=0;
  uint32_t original_pass_pixel_handle_entry_guest=0;
  uint32_t original_pass_vs_xdk_guest_object=0;
  uint32_t original_pass_ps_xdk_guest_object=0;
  bool original_pass_vs_ps_xdk_binding_verified=false;
  // Source 92-byte state from the EXACT original material pass selector;
  // needed to translate the genuine fixed-function Vulkan PSO later.
  std::array<std::byte,92> original_pass_state_record{};
  bool original_pass_state_record_captured=false;
  // USA 0x825F84C0 reads the original per-pass sampler-state record
  // count from +87, first record BE16 from +80, and the actual
  // material+196 four-byte sampler-state source table. Record byte0
  // &0x7f is the ORIGINAL XDK texture/sampler register. This is a
  // source-proven pass slot MASK, not proof that a guest XDK texture
  // object maps to a particular MODELS/Textures ARC image or fetch.
  uint16_t original_pass_sampler_slot_mask=0;
  bool original_pass_sampler_slots_verified=false;
  // At this exact original XDK Draw, every active pass slot has an
  // observed nonzero original device+12544 texture object pointer.
  // No texture payload SHA/PSO/format/mip identity inferred from that.
  bool original_pass_live_texture_objects_present=false;
  // Original 0x82642A88 invokes the shader instance's OWN virtual
  // model/surface item method (vtable slot+44) via PPC bctrl
  // 0x82642BD4. On that call r3 is the same source shader instance,
  // r4 is the ACTUAL per-render-item guest owner, r5 original
  // rendering context. This is 52 source-proven class mappings across
  // 22 unique original per-item virtual method addresses.
  // This does NOT by itself identify the model archive CRC32, the
  // surface ordinal or render-target pass; no invented model hash.
  uint32_t original_engine_item_method_guest=0;
  uint32_t original_engine_render_item_guest=0;
  uint32_t original_engine_render_item_context_guest=0;
  uint32_t original_engine_render_item_vtable_guest=0;
  // Original dispatch 0x82642B08 calls item vtable+48 and stores the
  // material return in PPC r27 at 0x82642B10. The vtable+44
  // render method receives this immutable r27 on indirect call
  // 0x82642BD4. This is a source item MATERIAL owner pointer, not
  // a ShaderEffects source hash or guaranteed shader stage digest.
  uint32_t original_engine_render_item_material_guest=0;
  bool original_engine_item_material_source_verified=false;
  bool original_engine_item_abi_verified=false;
};
// BeginVertices returns a writable inline allocation. EndVertices commits it
// without binding a VB, so the old device stream 0 is unrelated.
struct OriginalInlineGlyphBatch {
  uint32_t declaration=0, vertex_count=0;
  std::shared_ptr<const OriginalGuestBufferBytes> payload;
};
// These are original engine geometry wrappers, not a guessed direct
// BL caller: both wrappers tail-branch into the original XDK method.
// The retained LR therefore identifies their own source call site.
struct OriginalGeometryWrapperOwner {
  uint32_t geometry=0,engine=0,device=0,vertex=0,index=0,declaration=0;
  bool screen_quad=false;
};
struct RawDraw {
  Kind kind{};
  uint32_t function = 0, lr = 0;
  std::array<uint32_t, 8> gpr{}; // r3..r10 from original USA PPC call.
  // Read BEFORE the guest Draw mutates the XDK device shadow state.
  // A missing snapshot is not a verified camera or constant ABI.
  std::optional<OriginalVsMatrices> observed_vs_matrices;
  // Optional immutable full 256*float4 VS and PS banks. A shared pointer
  // avoids copying 8 KiB of guest data during mailbox/frame handoff.
  // Required for arbitrary original lighting/material constants, separate
  // from the c7..c25 camera/transform reflection.
  std::shared_ptr<const OriginalShaderRegisters> observed_shader_constants;
  std::shared_ptr<const OriginalGuestBufferBytes> source_vb0;
  std::shared_ptr<const OriginalGuestBufferBytes> source_ib;
  // The original 6534 XDK device can bind 16 independent vertex-stream
  // objects. Capture actual bytes at every USED slot during opt-in direct
  // EoR Vulkan replay; do not mistake VB0 for the other vertex attributes.
  // Slot 0 aliases source_vb0 for historical source mesh verification.
  std::array<std::shared_ptr<const OriginalGuestBufferBytes>,16>
      source_vertex_streams{};
  // Source-proven original shader virtual-method scope, if the actual
  // source Draw occurred before the corresponding engine method returned.
  // This is NOT a fabricated VerifiedXdkResourceWitness.
  std::optional<OriginalEorShaderMethodAtDraw> original_engine_shader_method;
  // Runtime content-addressed shader source identity from SAME Draw's
  // device stage objects, using their inline USA ShaderContainer headers.
  std::optional<aot::OriginalLiveXdkShaderHeaderWitness>
      observed_live_vertex_shader_header;
  std::optional<aot::OriginalLiveXdkShaderHeaderWitness>
      observed_live_pixel_shader_header;
  // Determined from TWO original live stage headers and the private
  // HEORMAT2 reverse VS/PS catalog at this exact PPC Draw, never inferred
  // from an unrelated shader class or adjacent EoR archive record.
  std::optional<aot::OriginalEffectPass> observed_live_eor_effect_pass;
  std::array<uint32_t,16> original_pixel_texture_keys{};
  // Captured actual ERTexture identity at this Draw, including bindings
  // outside a material scope. A bare archive hash cannot authorize it.
  OriginalTextureOwners original_pixel_texture_owners{};
  std::optional<OriginalInlineGlyphBatch> inline_glyphs;
  // Pixel fetch constant s0, copied at the same Font_Main Draw as its atlas.
  std::optional<std::array<uint32_t,6>> original_font_fetch;
  bool captured_vertex_bindings=false;
  std::array<std::optional<UsaVertexStreamBinding>,16> vertex_bindings{};
  std::optional<UsaIndexBufferBinding> index_binding;
  std::optional<UsaVertexDeclaration> vertex_declaration;
  std::optional<OriginalGeometryWrapperOwner> original_geometry_wrapper;
  OriginalPixelStageKind original_pixel_stage=OriginalPixelStageKind::Unknown;
};
// This classification is not itself a GPU submission authorization.
inline bool IsVerifiedVertexOnlyDepthDraw(const RawDraw& draw) noexcept {
  return draw.original_pixel_stage==OriginalPixelStageKind::VertexOnlyDepthStencil &&
      draw.observed_vs_matrices && draw.observed_live_vertex_shader_header &&
      !draw.observed_live_pixel_shader_header && !draw.observed_live_eor_effect_pass &&
      !draw.observed_vs_matrices->original_ps_guest_object &&
      draw.observed_vs_matrices->original_depth_guest_object;
}
inline bool HasOriginalDrawProgram(const RawDraw& draw) noexcept {
  return draw.observed_live_vertex_shader_header &&
      ((draw.observed_live_pixel_shader_header && draw.observed_live_eor_effect_pass) ||
       IsVerifiedVertexOnlyDepthDraw(draw));
}
// Original USA Decal direct XDK call is at guest 0x8265B538.
// An engine render-item->geometry witness, if present, must match the
// SAME live original PPC Draw device's bound VB/IB guest object identity.
// Absent/partial evidence NEVER becomes a geometry authority.
inline bool ValidOriginalDecalGeometryBinding(const RawDraw& call) noexcept {
  if(!call.original_engine_shader_method)return true;
  const auto& owner=*call.original_engine_shader_method;
  if(!owner.original_decal_xdk_vb_ib_binding_verified)
    return owner.original_decal_render_item_guest==0 &&
           owner.original_decal_geometry_guest==0;
  if(owner.original_shader_method_guest!=0x8265B420u ||
     owner.original_shader_vtable_guest!=0x82066504u ||
     !owner.original_shader_method_vtable_verified ||
     call.kind!=Kind::DrawIndexedVertices ||
     call.function!=0x8288E318u || call.lr!=0x8265B53Cu ||
     !owner.original_decal_render_item_guest ||
     !owner.original_decal_geometry_guest ||
     owner.original_decal_geometry_guest>UINT32_MAX-0x38u ||
     !call.observed_vs_matrices)
    return false;
  const auto& gpu=*call.observed_vs_matrices;
  return gpu.original_guest_device==call.gpr[0] &&
         gpu.original_vertex_buffer_guest_objects[0]==
             owner.original_decal_geometry_guest+0x18u &&
         gpu.original_index_buffer_guest_object==
             owner.original_decal_geometry_guest+0x38u;
}
// Original Decal item->material binder is independent of VB/IB geometry.
// It can only certify source pointer identity (source shader this+48,
// virtual item method slot+48 return, native 0x825F8A50 r4 descriptor),
// never infer a ShaderEffects effect hash or pixel/vertex shader pair.
inline bool ValidOriginalDecalItemMaterialBinding(
    const RawDraw& call) noexcept {
  if(!call.original_engine_shader_method)return true;
  const auto& owner=*call.original_engine_shader_method;
  if(!owner.original_decal_item_material_binding_verified)
    return !owner.original_decal_material_descriptor_guest &&
           !owner.original_decal_material_context_guest &&
           !owner.original_decal_material_word0 &&
           !owner.original_decal_material_word8;
  return owner.original_shader_method_guest==0x8265B420u &&
         owner.original_shader_vtable_guest==0x82066504u &&
         owner.original_shader_method_vtable_verified &&
         call.kind==Kind::DrawIndexedVertices &&
         call.lr==0x8265B53Cu &&
         owner.original_decal_xdk_vb_ib_binding_verified &&
         owner.original_decal_render_item_guest &&
         owner.original_decal_geometry_guest &&
         owner.original_decal_material_descriptor_guest &&
         owner.original_decal_material_context_guest;
}
// Original 0x82642A88 virtual dispatch +44 is the genuine EoR
// per-model/surface item renderer, executed with guest r3=this shader,
// r4=render item and r5=source rendering context. The XDK Draw beneath
// it retains the exact guest pointer to its owner. If absent, no
// independent model hash or surface ordinal can be invented.
inline bool ValidOriginalEorSourceRenderItem(
    const RawDraw& call) noexcept {
  if(!call.original_engine_shader_method)return true;
  const auto& source=*call.original_engine_shader_method;
  if(!source.original_engine_item_abi_verified)
    return source.original_engine_item_method_guest==0u &&
           source.original_engine_render_item_guest==0u &&
           source.original_engine_render_item_context_guest==0u &&
           source.original_engine_render_item_vtable_guest==0u &&
           source.original_engine_render_item_material_guest==0u &&
           !source.original_engine_item_material_source_verified;
  return source.original_shader_method_vtable_verified &&
         source.original_shader_instance_guest!=0u &&
         source.original_engine_render_item_guest!=0u &&
         source.original_engine_render_item_vtable_guest>=0x82000000u &&
         source.original_engine_render_item_vtable_guest<=0x821B1FCCu &&
         (source.original_engine_item_material_source_verified ?
          source.original_engine_render_item_material_guest!=0u :
          source.original_engine_render_item_material_guest==0u) &&
         ProvenUsaEorRenderItemMethodVtablePair(
             source.original_engine_item_method_guest,
             source.original_shader_vtable_guest);
}
// Exact original source EoR state slot → two shader handles → XDK Draw.
// This verifies guest source program object identities, not the original
// ShaderEffects SHA/VS/PS microcode or its source PDB authored ordinal.
// Only the actual XDK Draw's live device shadow may set verified=true.
inline bool ValidOriginalShaderPassStageBinding(
    const RawDraw& call) noexcept {
  if(!call.original_engine_shader_method)return true;
  const auto& owner=*call.original_engine_shader_method;
  if(!owner.original_pass_material_context_guest)
    return owner.original_pass_state_one_based==0u &&
           owner.original_pass_vertex_handle_entry_guest==0u &&
           owner.original_pass_pixel_handle_entry_guest==0u &&
           owner.original_pass_vs_xdk_guest_object==0u &&
           owner.original_pass_ps_xdk_guest_object==0u &&
           !owner.original_pass_vs_ps_xdk_binding_verified;
  if(!owner.original_shader_method_vtable_verified ||
     !ProvenUsaEorShaderMethodVtablePair(
         owner.original_shader_method_guest,
         owner.original_shader_vtable_guest) ||
     !owner.original_pass_state_one_based ||
     !owner.original_pass_vertex_handle_entry_guest ||
     !owner.original_pass_pixel_handle_entry_guest ||
     !owner.original_pass_vs_xdk_guest_object ||
     !owner.original_pass_ps_xdk_guest_object ||
     !owner.original_pass_vs_ps_xdk_binding_verified ||
     !call.observed_vs_matrices)
    return false;
  const auto& live=*call.observed_vs_matrices;
  return live.original_guest_device==call.gpr[0] &&
         live.original_vs_guest_object==owner.original_pass_vs_xdk_guest_object &&
         live.original_ps_guest_object==owner.original_pass_ps_xdk_guest_object;
}
// Per-pass XDK sampler STATE register writes are source-verified from the
// original EoR source program's +87 count/+80 first index and material
// +196 table. Shader sampling all *active textures* is not established:
// a slot in the state table proves a stage sampler *state operation*,
// not which ShaderEffects fetch/pixel program reads that texture.
inline bool ValidOriginalShaderPassSamplerStateAtDraw(
    const RawDraw& call) noexcept {
  if(!call.original_engine_shader_method)return true;
  const auto& owner=*call.original_engine_shader_method;
  if(!owner.original_pass_sampler_slots_verified)
    return owner.original_pass_sampler_slot_mask==0u &&
           !owner.original_pass_live_texture_objects_present;
  if(!ValidOriginalShaderPassStageBinding(call))return false;
  if(owner.original_pass_live_texture_objects_present) {
    if(!call.observed_vs_matrices)return false;
    for(uint32_t slot=0;slot<16u;++slot) {
      if((owner.original_pass_sampler_slot_mask&
          (uint16_t(1u)<<slot)) &&
         !call.observed_vs_matrices->original_texture_guest_objects[slot])
        return false;
    }
  }
  // True presence requires the EXACT live original device state,
  // but false presence is not promoted to a GPU texture proof.
  return true;
}
// The original geometry wrapper source code is fingerprinted separately
// by the live bridge. This routine independently checks the current
// callee-saved register owners and hardware device bindings. The actual
// vertex declaration, buffers, shaders and texture proof are still
// required for a native submission.
inline std::optional<OriginalGeometryWrapperOwner> ReadOriginalGeometryWrapperOwner(
    const RawDraw& call,uint32_t r30,uint32_t r31,ReadGuestBytes read,void* user) noexcept {
  if(!read || !r30 || !r31 || !call.observed_vs_matrices)return {};
  const bool quad=call.function==0x8288DF30u && call.lr==0x8268BFE4u &&
                  call.kind==Kind::DrawVertices;
  const bool indexed=call.function==0x8288E318u && call.lr==0x826676A8u &&
                     call.kind==Kind::DrawIndexedVertices;
  if(!quad && !indexed)return {};
  const uint32_t geometry=quad?r30:r31,engine=quad?r31:r30;
  const auto word=[&](uint32_t base,uint32_t offset)->std::optional<uint32_t>{
    std::array<std::byte,4> bytes{};
    if(!base || uint64_t(base)+offset+4u>(uint64_t{1}<<32) ||
       !read(user,base+offset,bytes.data(),bytes.size()))return {};
    return MaterialWord(bytes.data());
  };
  const auto device=word(engine,0u);
  const auto decl=word(geometry,quad?0xc30u:0x84u);
  const uint32_t vb_offset=quad?0xc58u:0x60u;
  if(uint64_t(geometry)+vb_offset>UINT32_MAX)return {};
  const auto vertex=geometry+vb_offset;
  const auto& live=*call.observed_vs_matrices;
  if(!device || !*device || *device!=call.gpr[0] ||
     live.original_guest_device!=*device || !decl || !*decl ||
     *decl!=live.original_vertex_declaration_guest_object ||
     vertex!=live.original_vertex_buffer_guest_objects[0])return {};
  OriginalGeometryWrapperOwner owner{geometry,engine,*device,vertex,0,*decl,quad};
  if(quad) {
    // 8268BFD0 passes two triangles, fan topology 6; the wrapper
    // converts these to four vertices via the original topology table.
    if(call.gpr[1]!=6u || call.gpr[2]!=0u || call.gpr[3]!=4u)return {};
  } else {
    // 8266768C loads geometry+16, divides by 3, and passes triangle
    // count; 82676288 multiplies by three for XDK indexed count.
    if(uint64_t(geometry)+0x30u>UINT32_MAX)return {};
    const auto count=word(geometry,16u);
    owner.index=geometry+0x30u;
    if(!count || !*count || *count>INT32_MAX || *count%3u ||
       call.gpr[1]!=4u || call.gpr[2] || call.gpr[3] ||
       call.gpr[4]!=*count ||
       owner.index!=live.original_index_buffer_guest_object)return {};
  }
  return owner;
}
inline bool OriginalGeometryWrapperHasValidParent(const RawDraw& call) noexcept {
  if(!call.original_engine_shader_method)return true;
  const auto& owner=*call.original_engine_shader_method;
  return owner.original_shader_method_vtable_verified &&
      owner.original_shader_instance_guest && owner.original_shader_vtable_guest &&
      owner.nesting_depth && owner.nesting_depth<=16u &&
      ProvenUsaEorShaderMethodVtablePair(owner.original_shader_method_guest,
                                          owner.original_shader_vtable_guest) &&
      ValidOriginalEorSourceRenderItem(call) &&
      ValidOriginalDecalGeometryBinding(call) &&
      ValidOriginalDecalItemMaterialBinding(call) &&
      ValidOriginalShaderPassStageBinding(call) &&
      ValidOriginalShaderPassSamplerStateAtDraw(call);
}
// Verified USA XDK 2.0.6534 method ABI from the original wrapper:
// DrawIndexedVertices(dev, prim, baseVertex, startIndex, indexCount).
// The primitive enum and guest buffer ownership remain real game state,
// NOT host Vulkan layout or model identity.
struct IndexedXdkCall {
  uint32_t xdk_function=0, caller_lr=0, guest_device=0;
  uint32_t primitive=0, first_index=0, index_count=0;
  int32_t base_vertex=0;
};
std::optional<IndexedXdkCall> DecodeOriginalIndexedXdkCall(const RawDraw& call);
// Verified XDK 6534 DrawVertices(dev, prim, startVertex, vertexCount).
// No UP/inline draw is inferred, and triangle-list topology must be
// independently matched to real source model/index data.
struct VerticesXdkCall {
  uint32_t xdk_function=0, caller_lr=0, guest_device=0;
  uint32_t primitive=0, start_vertex=0, vertex_count=0;
};
std::optional<VerticesXdkCall> DecodeOriginalVerticesXdkCall(const RawDraw& call);
std::optional<OriginalInlineGlyphBatch> CaptureUsaInlineGlyphs(
    const RawDraw& call,uint32_t wrapper,uint32_t wrapper_sp,
    ReadGuestBytes reader,void* state,OriginalGuestBufferSnapshots& buffers);
std::optional<std::array<uint32_t,6>> CaptureUsaFontFetch(
    uint32_t device, ReadGuestBytes read,void* state);
// Called only after the current Draw's texture owner recovery has finished.
inline std::optional<std::array<uint32_t,6>> CaptureUsaFontFetchAtDraw(
    const RawDraw& draw,ReadGuestBytes read,void* state) {
  const auto& parent=draw.original_engine_shader_method;
  const auto& live=draw.observed_vs_matrices;
  if(!parent || !live || !draw.gpr[0] ||
     live->original_guest_device!=draw.gpr[0] ||
     !parent->original_shader_method_vtable_verified ||
     parent->original_shader_method_guest!=0x8266A0C0u ||
     parent->original_shader_vtable_guest!=0x8206ED6Cu ||
     !parent->original_pass_vs_ps_xdk_binding_verified ||
     !parent->original_pass_sampler_slots_verified ||
     !(parent->original_pass_sampler_slot_mask&1u) ||
     !parent->original_pass_live_texture_objects_present ||
     !draw.original_pixel_texture_keys[0] ||
     !live->original_texture_guest_objects[0])return {};
  return CaptureUsaFontFetch(draw.gpr[0],read,state);
}
std::optional<VerticesXdkCall> DecodeOriginalFontVertices(const RawDraw& call);
// Original USA frontend DrawVertices prim=6 (D3DPT_TRIANGLEFAN) emits two
// triangles for a four-vertex quad; Vulkan's native DrawIndexed uses lists.
// Topology conversion is independent of archive/game resource ownership.
inline std::optional<uint32_t> VerifiedTriangleListCount(
    uint32_t primitive,uint32_t count) noexcept {
  if(primitive==4u && count>=3u && count%3u==0)return count;
  if(primitive==6u && count>=3u && count-2u<=UINT32_MAX/3u)
    return 3u*(count-2u);
  return std::nullopt;
}
// The genuine USA EoR 826676C0 wrapper sets primitive=1 and passes
// geometry+12 as vertex count via 826762C0 -> DrawVertices.
inline bool IsOriginalRuntimePointDraw(const RawDraw& call) noexcept {
  return call.kind==Kind::DrawVertices && call.function==0x8288DF30u &&
      call.lr==0x82667718u && call.gpr[0] && call.gpr[1]==1u &&
      call.gpr[2]==0u && call.gpr[3]!=0u;
}
inline std::optional<uint32_t> VerifiedRuntimeTriangleListCount(uint32_t primitive,uint32_t count) noexcept {
  if(primitive==5u && count>=3u && count-2u<=UINT32_MAX/3u)return 3u*(count-2u);
  if(primitive==13u && count>=4u && !(count%4u) && count/4u<=UINT32_MAX/6u)return count/4u*6u;
  return VerifiedTriangleListCount(primitive,count);
}
inline std::optional<uint32_t> VerifiedRuntimeIndexCount(
    const RawDraw& call,uint32_t primitive,uint32_t count) noexcept {
  if(primitive==1u)return IsOriginalRuntimePointDraw(call) && count==call.gpr[3] ?
      std::optional<uint32_t>{count}:std::nullopt;
  return VerifiedRuntimeTriangleListCount(primitive,count);
}
inline uint32_t OriginalTriangleFanVertex(
    uint32_t triangle,uint32_t corner) noexcept {
  return corner==0u ? 0u : triangle+corner;
}
struct Frame {
  uint64_t sequence = 0;
  uint32_t dropped = 0;
  std::vector<RawDraw> calls;
  // Explicit interleaved original XDK command order, optional only for
  // historical diagnostics. Required for future independent presentation.
  std::vector<RawCommand> commands;
  uint32_t dropped_commands = 0;
};
// A complete native frame must include EXACTLY the recorded Draw subsequence
// and one terminal Present; discarded commands invalidate the full scene.
bool ValidateOriginalCommandTimeline(const Frame& original) noexcept;
// Single-slot frame handoff for the native renderer. Never block guest
// Present waiting for Vulkan; any overwritten frame is explicitly counted.
struct DeliveredFrame {
  Frame frame;
  uint64_t overwritten_since_take=0;
};
class CompletedFrameMailbox {
 public:
  void Publish(Frame frame);
  std::optional<DeliveredFrame> Take();
  // Immutable preview outlives the mailbox lock, allowing Vulkan source
  // eligibility checks without blocking the guest PPC Present thread.
  // Drop all previews before calling TakeIfSequence.
  std::shared_ptr<const Frame> Peek() const noexcept {return pending_;}
  uint64_t MissedSinceTake() const noexcept {return missed_since_take_;}
  // Atomically reject a stale frame after another original guest Present.
  std::optional<DeliveredFrame> TakeIfSequence(uint64_t source_sequence);
  // Claim one complete, non-overwritten Present without moving its storage.
  // A later guest Present can now publish independently while the sole
  // renderer builds PSOs/materials for this immutable frame.
  std::shared_ptr<const Frame> ClaimCleanIfSequence(uint64_t source_sequence) noexcept;
  // The independent complete-target-graph producer may claim the latest
  // immutable epoch even when an older complete Present was overwritten.
  // This is a reservation, NOT authorization: the caller must verify
  // every clear, attachment, Resolve, Draw and shader of the claimed frame.
  // External source-witness/legacy replay still requires ClaimClean.
  std::shared_ptr<const Frame> ClaimLatestIndependentIfSequence(
      uint64_t source_sequence) noexcept;
  // A missed Present cannot be replayed. Drop the pending epoch by its
  // exact source sequence EVEN WHILE other threads retain read-only Peek()
  // snapshots. Unlike Take(), this never moves data still being inspected.
  // Used only for loss recovery, not to authorize any native GPU Draw.
  bool DiscardIfSequence(uint64_t source_sequence) noexcept;
  uint64_t TotalOverwritten() const noexcept {return total_overwritten_;}
  void Reset() noexcept;
 private:
  std::shared_ptr<Frame> pending_;
  uint64_t total_overwritten_=0;
  uint64_t missed_since_take_=0;
};
class FrameCapture {
 public:
  static constexpr std::size_t kMaxDraws = 8192;
  static constexpr std::size_t kMaxCommands = 65536;
  // Only call under the same collector mutex as Record()/Finish().
  // These values label the authentic next Present and Draw, not a
  // synthetic draw-order index created by the Vulkan worker.
  uint64_t NextFrameSequence() const noexcept {return seq_+1;}
  uint32_t NextDrawOrdinal() const noexcept {
    return static_cast<uint32_t>(calls_.size());
  }
  void Record(RawDraw call);
  void RecordCommand(const RawCommand& command);
  Frame Finish();
  void Reset();
 private:
  uint64_t seq_ = 0;
  uint32_t dropped_ = 0;
  uint32_t dropped_commands_ = 0;
  std::vector<RawDraw> calls_;
  std::vector<RawCommand> commands_;
};
// Read-only source-proven guest callsite frequency and ABI variability.
// An LR identifies a callsite lead, NOT an original engine method or layout.
struct CallsiteEvidenceRecord {
  Kind kind{};
  uint32_t xdk_function=0;
  uint32_t caller_lr=0;
  uint64_t recorded_draws=0;
  uint64_t frames_seen=0;
  uint64_t first_frame=0;
  uint64_t last_frame=0;
  uint32_t varying_gpr_mask=0;
  uint32_t nonzero_gpr_mask=0;
  std::array<uint32_t,8> first_gpr{};
};
class XdkCallsiteEvidence {
 public:
  static constexpr std::size_t kMaxCallsites=256;
  void Observe(const Frame& frame);
  std::vector<CallsiteEvidenceRecord> HotCallsites(std::size_t count=8) const;
  uint64_t DroppedDraws() const noexcept {return dropped_draws_;}
  uint64_t UntrackedCalls() const noexcept {return untracked_calls_;}
  void Reset() noexcept;
 private:
  using Key=std::tuple<Kind,uint32_t,uint32_t>;
  std::map<Key,CallsiteEvidenceRecord> records_;
  uint64_t last_sequence_=0;
  uint64_t dropped_draws_=0;
  uint64_t untracked_calls_=0;
};
// These methods are only called from verified hulk_native_x_profile.ini
// addresses in the AOT USA wrappers. Raw PPC r3..r10 are not host VB/IB
// identities or rasterizable shader programs.
}  // namespace hulk::eor::xdk
