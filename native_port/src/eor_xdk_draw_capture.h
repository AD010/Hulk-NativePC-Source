// Actual USA XDK call provenance, NOT a fabricated EoR Draw submission.
// Keep original PPC call-through and memory untouched. Opt-in audit only.
#pragma once
#include "eor_xdk_guest_constants.h"
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
enum class Kind : uint8_t { DrawVertices, DrawIndexedVertices, DrawVerticesUP };
// These six bl instructions were observed in the PRIVATE AOT corpus from
// SHA-pinned USA default.xex (98,726 generated PPC function definitions).
// r3..r6 / r3..r7 are independently verified source XDK method arguments.
// A direct bl sets LR to its next instruction; a TAIL branch (b) does not.
// Never resolve a tail-call LR to a presumed engine Draw owner, and never
// claim that knowing a caller proves its model, material, effect or shader.
inline constexpr uint32_t ProvenUsaXdkDirectBlCaller(
    uint32_t xdk_function,uint32_t source_lr) noexcept {
  switch(xdk_function) {
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
  ClearF, Resolve, BeginTiling, EndTiling, Present
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
// Single-main-color/depth native Vulkan backend can clear a source RECT
// only if the RECT spans the ENTIRE original output target. Partial clears
// need a separate native scissored/quad implementation, not a guessed clear.
inline bool IsOriginalFullTargetClear(
    const OriginalXdkClearF& clear,uint32_t original_width,
    uint32_t original_height) noexcept {
  if(!original_width || !original_height ||
     (clear.flags & ~0x31u)!=0 || (clear.flags & 0x31u)==0 ||
     ((clear.flags&1u)!=0 && !clear.has_color) ||
     ((clear.flags&0x10u)!=0 &&
      (!std::isfinite(clear.depth) || clear.depth<0.f || clear.depth>1.f)))
    return false;
  if(!clear.has_rect)return true;
  return clear.rect[0]==0 && clear.rect[1]==0 &&
         clear.rect[2]==static_cast<int32_t>(original_width) &&
         clear.rect[3]==static_cast<int32_t>(original_height);
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
};
struct OriginalGuestBufferBytes {
  uint32_t guest_object=0,guest_address=0,guest_size=0;
  std::vector<std::byte> bytes;
};
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
  bool original_engine_item_abi_verified=false;
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
};
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
  void Record(const RawDraw& call);
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
