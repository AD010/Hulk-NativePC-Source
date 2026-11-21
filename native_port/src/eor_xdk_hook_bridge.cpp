// Read-only connection to REAL Hulk USA XDK Draw and Present wrappers.
// Never writes guest memory, never bypasses the original NativeX or Xenos.
// This is an input provenance oracle, not a completed Vulkan renderer.
#include "eor_xdk_hook_bridge.h"
#include "device.h"
#include "SwapChain.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <limits>
#include <mutex>
#include <memory>
#include <new>
#include <utility>
#include <variant>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(hulk_eor_xdk_capture, false, "Hulk/Diagnostics",
                    "Opt-in original XDK Draw/Present call snapshot; no rendering redirection");
REXCVAR_DEFINE_BOOL(hulk_eor_vulkan_replay, false, "Hulk/Diagnostics",
                    "Opt-in direct EoR Vulkan replay; requires verified sole presenter");
namespace hulk::eor::xdk {
namespace {
std::mutex g_mutex;
std::atomic<uint64_t> g_eor_ready_backbuffers{0};
std::atomic<uint64_t> g_eor_gpu_failures{0};
FrameCapture g_collector;
XdkCallsiteEvidence g_callsite_evidence;
CompletedFrameMailbox g_frames_for_native;
std::shared_ptr<const VerifiedWorkerEorFrame> g_verified_worker_frame;
struct OfflineShaderPack {
  std::shared_ptr<const aot::ShaderBank> shaders;
  std::shared_ptr<const aot::EffectCatalog> effects;
  std::shared_ptr<const aot::ReflectionBank> reflection;
  std::shared_ptr<const aot::OriginalXdkShaderSamplers> samplers;
  std::shared_ptr<const aot::OriginalVirtualShaderIdentity> virtual_ids;
};
std::optional<OfflineShaderPack> g_offline_shader_pack;
OriginalGuestBufferSnapshots g_original_buffers; // guarded by g_mutex
// The actual guest EoR shader method and its inner XDK Draw execute on
// the SAME generated PPC thread. A fixed TLS stack retains the source
// method scope without allocations, guest writes or global frame races.
// The genuine engine shader vtable identity is verified before push.
thread_local std::array<OriginalEorShaderMethodAtDraw,16>
    g_source_shader_method_stack{};
thread_local uint8_t g_source_shader_method_depth=0;
thread_local uint32_t g_source_shader_rejected_nesting=0;
// ReXGlue's guest base is NOT directly an original Xbox VA; use the same
// guest high-memory padding convention as native_camera_direct.cpp, and
// reject inaccessible ranges without reading unverified game memory.
bool ReadGuest(void* state,uint32_t address,std::byte* output,size_t length) noexcept {
#ifdef _WIN32
  auto* guest_base=static_cast<uint8_t*>(state);
  if(!guest_base || !output || !length)return false;
  const uint64_t offset=uint64_t(address)+(address>=0xE0000000u?4096u:0u);
  if(offset>UINTPTR_MAX-uintptr_t(guest_base))return false;
  const uintptr_t start=uintptr_t(guest_base)+uintptr_t(offset);
  if(length>UINTPTR_MAX-start)return false;
  // A shader/declaration/texture state block may cross two committed
  // regions with different protections. Walk every region independently,
  // copying only after verifying that part; never blindly span a gap.
  uintptr_t cursor=start;
  size_t remaining=length;
  auto* destination=output;
  while(remaining) {
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(reinterpret_cast<const void*>(cursor),&info,
                    sizeof(info))!=sizeof(info) ||
       info.State!=MEM_COMMIT || (info.Protect&PAGE_GUARD))
      return false;
    const DWORD protection=info.Protect & 0xffu;
    if(protection!=PAGE_READONLY && protection!=PAGE_READWRITE &&
       protection!=PAGE_WRITECOPY && protection!=PAGE_EXECUTE_READ &&
       protection!=PAGE_EXECUTE_READWRITE &&
       protection!=PAGE_EXECUTE_WRITECOPY)
      return false;
    const uintptr_t region=uintptr_t(info.BaseAddress);
    if(info.RegionSize>UINTPTR_MAX-region)return false;
    const uintptr_t end=region+info.RegionSize;
    if(cursor<region || cursor>=end)return false;
    const auto chunk=std::min<size_t>(remaining,end-cursor);
    std::memcpy(destination,reinterpret_cast<const void*>(cursor),chunk);
    destination+=chunk;
    remaining-=chunk;
    cursor+=chunk;
  }
  return true;
#else
  (void)state;(void)address;(void)output;(void)length;
  return false;
#endif
}
}

void ObserveOriginalEorVmState_r(
    uint32_t original_method_guest,const PPCContext& original_ctx,
    uint8_t* original_guest_base) noexcept {
  if(!CaptureEnabled() || original_method_guest!=0x8264C798u ||
     !original_guest_base || !original_ctx.r3.u32)
    return;
  const uint32_t call_lr=static_cast<uint32_t>(original_ctx.lr);
  if(call_lr<0x82000000u || call_lr>=0x84000000u ||
     (call_lr&3u))
    return;
  // USA original native state_r is registered in .rdata as
  // 0x82E2EB8C -> (state_r,0x8264C798). PPC method r3 is an
  // original VM call context, not a UIPage_Pause_Settings pointer.
  // The first two levels are SOURCE OBSERVATIONS only; no VM type
  // offsets or page identities are invented.
  const auto read32=[&](uint32_t guest)->std::optional<uint32_t> {
    if(!guest || guest>UINT32_MAX-4u)return std::nullopt;
    std::array<std::byte,4> raw{};
    if(!ReadGuest(original_guest_base,guest,raw.data(),raw.size()))
      return std::nullopt;
    return uint32_t(std::to_integer<uint8_t>(raw[0]))<<24 |
           uint32_t(std::to_integer<uint8_t>(raw[1]))<<16 |
           uint32_t(std::to_integer<uint8_t>(raw[2]))<<8 |
           uint32_t(std::to_integer<uint8_t>(raw[3]));
  };
  // Exact original USA state_r PPC source:
  //   0x8264C7AC: frame=BE32(r3+0)
  //   0x8264C7B0: inputOwner=BE32(frame+0)
  //   0x8264C7B4: returnObject=BE32(frame+4)
  //   0x8264C7B8: nested=BE32(inputOwner+8)
  //   0x8264C7BC: stateOwner=BE32(nested+0)
  //   0x8264C7C0: requestedState=BE32(stateOwner+12)
  //   0x8264C7C4: BE32(returnObject+12)=requestedState.
  // These pointers are ORIGINAL guest VM objects, not the original page
  // NAME/ID. Never invoke PC settings on this shared global callback.
  const auto vm_frame=read32(original_ctx.r3.u32);
  const auto input=vm_frame ? read32(*vm_frame):std::nullopt;
  const auto output=vm_frame && *vm_frame<=UINT32_MAX-4u
      ? read32(*vm_frame+4u) : std::nullopt;
  const auto nested=input && *input<=UINT32_MAX-8u
      ? read32(*input+8u) : std::nullopt;
  const auto owner=nested ? read32(*nested) : std::nullopt;
  const auto requested=owner && *owner<=UINT32_MAX-12u
      ? read32(*owner+12u) : std::nullopt;
  const auto previous=output && *output<=UINT32_MAX-12u
      ? read32(*output+12u) : std::nullopt;
  static std::atomic<uint64_t> calls{0};
  const auto n=calls.fetch_add(1,std::memory_order_relaxed)+1u;
  // Bound log cost and preserve only original PPC virtual addresses.
  // This common VM getter/setter must NEVER be promoted to a native
  // Windows menu event merely because ChangeState_r calls it.
  if(n<=8u || (n%8192u)==0u)
    REXLOG_INFO("[HULK/EOR-VM-STATE-R] calls={} original_native=0x{:08X} "
                "guest_LR=0x{:08X} VM_r3=0x{:08X} frame=0x{:08X} "
                "input_owner=0x{:08X} output_owner=0x{:08X} "
                "vm_state_before=0x{:08X} vm_state_requested=0x{:08X} "
                "complete_ppc_source_chain={}; "
                "PAGE-OWNER UNKNOWN, not native Pause Settings event",
                n,original_method_guest,call_lr,original_ctx.r3.u32,
                vm_frame.value_or(0u),input.value_or(0u),
                output.value_or(0u),previous.value_or(0u),
                requested.value_or(0u),
                static_cast<bool>(requested && previous));
}

void ObserveOriginalEorShaderPass(
    uint32_t original_method_guest,const PPCContext& original_ctx,
    uint8_t* original_guest_base) noexcept {
  if(!CaptureEnabled() || original_method_guest!=0x825F84C0u ||
     !original_guest_base || !g_source_shader_method_depth ||
     g_source_shader_rejected_nesting)return;
  auto& scope=g_source_shader_method_stack[g_source_shader_method_depth-1u];
  // Every source 0x825F84C0 pass resets the existing stage evidence.
  // If a new pass fails to materialize, the previous pass CANNOT be
  // carried over to a Draw from a different material/state slot.
  scope.original_pass_material_context_guest=0u;
  scope.original_pass_state_one_based=0u;
  scope.original_pass_vertex_handle_entry_guest=0u;
  scope.original_pass_pixel_handle_entry_guest=0u;
  scope.original_pass_vs_xdk_guest_object=0u;
  scope.original_pass_ps_xdk_guest_object=0u;
  scope.original_pass_vs_ps_xdk_binding_verified=false;
  scope.original_pass_sampler_slot_mask=0u;
  scope.original_pass_sampler_slots_verified=false;
  scope.original_pass_live_texture_objects_present=false;
  if(!scope.original_shader_method_vtable_verified ||
     !ProvenUsaEorShaderMethodVtablePair(
         scope.original_shader_method_guest,
         scope.original_shader_vtable_guest) ||
     !scope.original_shader_instance_guest ||
     scope.original_shader_instance_guest>UINT32_MAX-48u ||
     original_ctx.r4.u32==0u || original_ctx.r4.u32>512u ||
     original_ctx.r3.u32==0u ||
     original_ctx.r3.u32>UINT32_MAX-196u)
    return;
  const auto read32=[&](uint32_t guest)->std::optional<uint32_t> {
    if(!guest || guest>UINT32_MAX-4u)return std::nullopt;
    std::array<std::byte,4> bytes{};
    if(!ReadGuest(original_guest_base,guest,bytes.data(),bytes.size()))
      return std::nullopt;
    return uint32_t(std::to_integer<uint8_t>(bytes[0]))<<24 |
           uint32_t(std::to_integer<uint8_t>(bytes[1]))<<16 |
           uint32_t(std::to_integer<uint8_t>(bytes[2]))<<8 |
           uint32_t(std::to_integer<uint8_t>(bytes[3]));
  };
  // USA EoR class methods e.g. 0x826432F0 load shader+48 as the
  // material context passed to 0x825F84C0. It is not XDK device.
  const auto material_ctx=read32(scope.original_shader_instance_guest+48u);
  if(!material_ctx || *material_ctx!=original_ctx.r3.u32)return;
  const auto records=read32(*material_ctx+112u);
  const auto vstable=read32(*material_ctx+124u);
  const auto pstable=read32(*material_ctx+136u);
  if(!records || !vstable || !pstable ||
     !*records || !*vstable || !*pstable)return;
  const uint64_t start=uint64_t(*records)+
      uint64_t(original_ctx.r4.u32-1u)*92u;
  if(start>UINT32_MAX-90u)return;
  // The original material pass selector 0x825F84C0 reads byte
  // slot indexes at original 92-byte state record +84 (VS), +85 (PS),
  // multiplying each by 8 and reading the stage table entry's first
  // BE32 guest shader object ID. This is independently confirmed by
  // 0x82675C70/0x82675D20 and XDK 0x82874390/0x82874090.
  // Read the actual original 92-byte shader-state record starting at
  // +80 (BE16 sampler table index), +84/+85 VS/PS stage indexes,
  // +87 count of 4-byte original sampler-state records. Only original
  // source data is read; no host bindings or textures are invented.
  std::array<std::byte,10> shader_slots{};
  if(!ReadGuest(original_guest_base,static_cast<uint32_t>(start+80u),
                shader_slots.data(),shader_slots.size()))return;
  const uint32_t vs_index=std::to_integer<uint8_t>(shader_slots[4]);
  const uint32_t ps_index=std::to_integer<uint8_t>(shader_slots[5]);
  const uint64_t vs_entry=uint64_t(*vstable)+8u*vs_index;
  const uint64_t ps_entry=uint64_t(*pstable)+8u*ps_index;
  if(vs_entry>UINT32_MAX-4u || ps_entry>UINT32_MAX-4u)return;
  const auto bound_vs=read32(static_cast<uint32_t>(vs_entry));
  const auto bound_ps=read32(static_cast<uint32_t>(ps_entry));
  if(!bound_vs || !bound_ps || !*bound_vs || !*bound_ps)return;
  scope.original_pass_material_context_guest=*material_ctx;
  scope.original_pass_state_one_based=original_ctx.r4.u32;
  scope.original_pass_vertex_handle_entry_guest=static_cast<uint32_t>(vs_entry);
  scope.original_pass_pixel_handle_entry_guest=static_cast<uint32_t>(ps_entry);
  scope.original_pass_vs_xdk_guest_object=*bound_vs;
  scope.original_pass_ps_xdk_guest_object=*bound_ps;
  const uint32_t active_sampler_records=
      std::to_integer<uint8_t>(shader_slots[7]);
  if(active_sampler_records<=16u) {
    bool valid=true;
    uint16_t mask=0u;
    if(active_sampler_records) {
      const auto sampler_table=read32(*material_ctx+196u);
      const uint32_t first=(
          uint32_t(std::to_integer<uint8_t>(shader_slots[0]))<<8) |
          uint32_t(std::to_integer<uint8_t>(shader_slots[1]));
      const uint64_t begin=sampler_table ?
          uint64_t(*sampler_table)+uint64_t(first)*4u : 0u;
      if(!sampler_table || !*sampler_table ||
         begin>UINT32_MAX-active_sampler_records*4u) {
        valid=false;
      } else {
        for(uint32_t i=0;i<active_sampler_records;++i) {
          std::array<std::byte,4> record{};
          if(!ReadGuest(original_guest_base,
                        static_cast<uint32_t>(begin+uint64_t(i)*4u),
                        record.data(),record.size())) {
            valid=false;break;
          }
          // The original EoR method masks off high type bits before
          // passing the source sampler register to 0x82675E00.
          const uint32_t slot=std::to_integer<uint8_t>(record[0])&0x7fu;
          if(slot>=16u || (mask&(uint16_t(1u)<<slot))) {
            valid=false;break;
          }
          mask|=uint16_t(1u)<<slot;
        }
      }
    }
    if(valid) {
      scope.original_pass_sampler_slot_mask=mask;
      scope.original_pass_sampler_slots_verified=true;
    }
  }
  // Only the later real XDK Draw's captured device shadow, not the
  // EoR setter itself, may prove stage GPU objects and sampler-bound
  // original texture object IDs are live.
}

void ObserveOriginalEorMaterialDescriptor(
    uint32_t original_method_guest,const PPCContext& original_ctx,
    uint8_t* original_guest_base) noexcept {
  if(!CaptureEnabled() || original_method_guest!=0x825F8A50u ||
     !original_guest_base || !g_source_shader_method_depth ||
     g_source_shader_rejected_nesting)
    return;
  auto& scope=g_source_shader_method_stack[g_source_shader_method_depth-1u];
  if(scope.original_shader_method_guest!=0x8265B420u ||
     scope.original_shader_vtable_guest!=0x82066504u ||
     !scope.original_shader_method_vtable_verified)
    return;
  const uint32_t source_lr=static_cast<uint32_t>(original_ctx.lr);
  // In the original source 0x82642C68, the first material call
  // 0x82642C90 installs the default. ALWAYS invalidate the preceding
  // per-item binding, otherwise a stale descriptor could be attached
  // to the next Draw when the real item virtual method fails to bind.
  if(source_lr==0x82642C94u) {
    scope.original_decal_material_descriptor_guest=0;
    scope.original_decal_material_context_guest=0;
    scope.original_decal_material_word0=0;
    scope.original_decal_material_word8=0;
    scope.original_decal_item_material_binding_verified=false;
    return;
  }
  // 0x82642C94 reads the item vtable, calls its slot+48 at +0xCA4,
  // then 0x82642CA8 passes the virtual return in PPC r4 to the
  // original native setter at 0x82642CB0, returning LR +4.
  if(source_lr!=0x82642CB4u || !original_ctx.r4.u32 ||
     scope.original_shader_instance_guest>UINT32_MAX-52u)
    return;
  const auto be32=[](const std::byte* p) noexcept {
    return uint32_t(std::to_integer<uint8_t>(p[0]))<<24 |
           uint32_t(std::to_integer<uint8_t>(p[1]))<<16 |
           uint32_t(std::to_integer<uint8_t>(p[2]))<<8 |
           uint32_t(std::to_integer<uint8_t>(p[3]));
  };
  // PPC source: Decal's r3 shader instance at virtual method entry
  // becomes r31 in 0x82642C68 and r3=BE32(shader+48) at 0x82642CAC.
  // Validate that this is genuinely the same game material context.
  std::array<std::byte,4> source_context{};
  if(!ReadGuest(original_guest_base,
                scope.original_shader_instance_guest+48u,
                source_context.data(),source_context.size()) ||
     be32(source_context.data())!=original_ctx.r3.u32)
    return;
  // Actual native material setter PPC loads r4[8] and r4[0].
  // Snapshot both words at the original call; these are opaque source
  // material IDENTIFIERS, not verified effect/PS/Vulkan resources.
  if(original_ctx.r4.u32>UINT32_MAX-12u)return;
  std::array<std::byte,12> material{};
  if(!ReadGuest(original_guest_base,original_ctx.r4.u32,
                material.data(),material.size()))
    return;
  scope.original_decal_material_descriptor_guest=original_ctx.r4.u32;
  scope.original_decal_material_context_guest=original_ctx.r3.u32;
  scope.original_decal_material_word0=be32(material.data());
  scope.original_decal_material_word8=be32(material.data()+8);
  scope.original_decal_item_material_binding_verified=true;
}

ScopedOriginalEorShaderMethod::ScopedOriginalEorShaderMethod(
    uint32_t original_method_guest,const PPCContext& original_ctx,
    uint8_t* original_guest_base) noexcept {
  // The same original method entry is used by 33 distinct shader classes.
  // Only the LIVE original this[0] vtable disambiguates its real owner.
  if(!ProvenUsaEorShaderMethodEntry(original_method_guest) ||
     !CaptureEnabled())return;
  const auto suppress_unverified=[&]() noexcept {
    ++g_source_shader_rejected_nesting;
    suppressed_=true;
  };
  if(g_source_shader_rejected_nesting || !original_guest_base ||
     !original_ctx.r3.u32 ||
     g_source_shader_method_depth>=g_source_shader_method_stack.size()) {
    suppress_unverified();
    return;
  }
  // Original PPC object layout: first BE32 is a real .rdata shader vtable.
  // The game memory is probed with checked VirtualQuery and never mutated.
  std::array<std::byte,4> guest_vtable{};
  if(!ReadGuest(original_guest_base,original_ctx.r3.u32,
                guest_vtable.data(),guest_vtable.size())) {
    suppress_unverified();
    return;
  }
  const uint32_t actual_vtable=
      uint32_t(std::to_integer<uint8_t>(guest_vtable[0]))<<24 |
      uint32_t(std::to_integer<uint8_t>(guest_vtable[1]))<<16 |
      uint32_t(std::to_integer<uint8_t>(guest_vtable[2]))<<8 |
      uint32_t(std::to_integer<uint8_t>(guest_vtable[3]));
  if(!ProvenUsaEorShaderMethodVtablePair(
         original_method_guest,actual_vtable)) {
    suppress_unverified();
    return;
  }
  OriginalEorShaderMethodAtDraw original{};
  original.original_shader_method_guest=original_method_guest;
  original.original_shader_instance_guest=original_ctx.r3.u32;
  original.original_shader_vtable_guest=actual_vtable;
  original.source_r4_through_r7={
      original_ctx.r4.u32,original_ctx.r5.u32,
      original_ctx.r6.u32,original_ctx.r7.u32};
  original.nesting_depth=
      static_cast<uint8_t>(g_source_shader_method_depth+1);
  original.original_shader_method_vtable_verified=true;
  g_source_shader_method_stack[g_source_shader_method_depth++]=original;
  active_=true;
}
ScopedOriginalEorShaderMethod::~ScopedOriginalEorShaderMethod() noexcept {
  if(suppressed_ && g_source_shader_rejected_nesting)
    --g_source_shader_rejected_nesting;
  if(active_ && g_source_shader_method_depth)
    --g_source_shader_method_depth;
}

ScopedOriginalEorRenderItem::ScopedOriginalEorRenderItem(
    uint32_t original_method_guest,const PPCContext& original_ctx,
    uint8_t* original_guest_base) noexcept {
  if(!CaptureEnabled() || !g_source_shader_method_depth ||
     g_source_shader_rejected_nesting ||
     !ProvenUsaEorRenderItemMethodEntry(original_method_guest))
    return;
  shader_source_depth_=g_source_shader_method_depth;
  auto& parent=g_source_shader_method_stack[shader_source_depth_-1u];
  owning_shader_instance_=parent.original_shader_instance_guest;
  previous_method_=parent.original_engine_item_method_guest;
  previous_item_=parent.original_engine_render_item_guest;
  previous_context_=parent.original_engine_render_item_context_guest;
  previous_item_vtable_=parent.original_engine_render_item_vtable_guest;
  previous_material_=parent.original_engine_render_item_material_guest;
  previous_material_verified_=parent.original_engine_item_material_source_verified;
  previous_verified_=parent.original_engine_item_abi_verified;
  touched_=true;
  // Every sibling item re-enters via 0x82642BD4. NEVER reuse an old
  // item owner's source pointer if the next call is malformed, from
  // another engine renderer or missing a guest shader instance.
  parent.original_engine_item_method_guest=0u;
  parent.original_engine_render_item_guest=0u;
  parent.original_engine_render_item_context_guest=0u;
  parent.original_engine_render_item_vtable_guest=0u;
  parent.original_engine_render_item_material_guest=0u;
  parent.original_engine_item_material_source_verified=false;
  parent.original_engine_item_abi_verified=false;
  if(!original_guest_base || !original_ctx.r3.u32 ||
     !original_ctx.r4.u32 ||
     static_cast<uint32_t>(original_ctx.lr)!=0x82642BD8u ||
     original_ctx.r3.u32!=parent.original_shader_instance_guest ||
     original_ctx.r31.u32!=original_ctx.r3.u32 ||
     original_ctx.r28.u32!=original_ctx.r4.u32 ||
     original_ctx.r23.u32!=original_ctx.r5.u32)
    return;
  std::array<std::byte,4> vtable_bytes{};
  if(!ReadGuest(original_guest_base,original_ctx.r3.u32,
                vtable_bytes.data(),vtable_bytes.size()))return;
  const uint32_t vtable=
      uint32_t(std::to_integer<uint8_t>(vtable_bytes[0]))<<24 |
      uint32_t(std::to_integer<uint8_t>(vtable_bytes[1]))<<16 |
      uint32_t(std::to_integer<uint8_t>(vtable_bytes[2]))<<8 |
      uint32_t(std::to_integer<uint8_t>(vtable_bytes[3]));
  if(vtable!=parent.original_shader_vtable_guest ||
     !ProvenUsaEorRenderItemMethodVtablePair(
         original_method_guest,vtable))return;
  // At source PPC 0x82642AF8 .. 0x82642B10, the original
  // EoR dispatcher called render_item->vtable+48 to obtain the
  // original source MATERIAL in r3 and saved it in callee-saved r27.
  // At bctrl 0x82642BD4, unchanged r27 therefore belongs to this
  // exact r28/r4 render item. Validate the original item's true
  // virtual table and material method before accepting the source owner.
  std::array<std::byte,4> item_vtable_bytes{};
  if(!ReadGuest(original_guest_base,original_ctx.r4.u32,
                item_vtable_bytes.data(),item_vtable_bytes.size()))
    return;
  const uint32_t item_vtable=
      uint32_t(std::to_integer<uint8_t>(item_vtable_bytes[0]))<<24 |
      uint32_t(std::to_integer<uint8_t>(item_vtable_bytes[1]))<<16 |
      uint32_t(std::to_integer<uint8_t>(item_vtable_bytes[2]))<<8 |
      uint32_t(std::to_integer<uint8_t>(item_vtable_bytes[3]));
  if(item_vtable<0x82000000u || item_vtable>0x821B1FCCu)
    return; // source .rdata, plus the real slot+48 pointer
  std::array<std::byte,4> material_method_bytes{};
  if(!ReadGuest(original_guest_base,item_vtable+48u,
                material_method_bytes.data(),material_method_bytes.size()))
    return;
  const uint32_t material_method=
      uint32_t(std::to_integer<uint8_t>(material_method_bytes[0]))<<24 |
      uint32_t(std::to_integer<uint8_t>(material_method_bytes[1]))<<16 |
      uint32_t(std::to_integer<uint8_t>(material_method_bytes[2]))<<8 |
      uint32_t(std::to_integer<uint8_t>(material_method_bytes[3]));
  if(material_method<0x821B2000u || material_method>=0x82D61523u ||
     (material_method&3u))
    return;
  parent.original_engine_item_method_guest=original_method_guest;
  parent.original_engine_render_item_guest=original_ctx.r4.u32;
  parent.original_engine_render_item_context_guest=original_ctx.r5.u32;
  parent.original_engine_render_item_vtable_guest=item_vtable;
  parent.original_engine_item_abi_verified=true;
  if(original_ctx.r27.u32) {
    std::array<std::byte,4> source_material{};
    if(ReadGuest(original_guest_base,original_ctx.r27.u32,
                 source_material.data(),source_material.size())) {
      parent.original_engine_render_item_material_guest=
          original_ctx.r27.u32;
      parent.original_engine_item_material_source_verified=true;
    }
  }
}
ScopedOriginalEorRenderItem::~ScopedOriginalEorRenderItem() noexcept {
  // Nested item invocations are permitted, but only restore into the
  // EXACT original source shader frame. Never revive stale pointers
  // after a different shader nested on top or capture was disabled.
  if(!touched_ || !shader_source_depth_ ||
     shader_source_depth_!=g_source_shader_method_depth)return;
  auto& parent=g_source_shader_method_stack[shader_source_depth_-1u];
  if(parent.original_shader_instance_guest!=owning_shader_instance_)
    return; // original owning shader scope changed; never revive old owner
  parent.original_engine_item_method_guest=previous_method_;
  parent.original_engine_render_item_guest=previous_item_;
  parent.original_engine_render_item_context_guest=previous_context_;
  parent.original_engine_render_item_vtable_guest=previous_item_vtable_;
  parent.original_engine_render_item_material_guest=previous_material_;
  parent.original_engine_item_material_source_verified=
      previous_material_verified_;
  parent.original_engine_item_abi_verified=previous_verified_;
}

// Original USA XDK 6534 resource object: GPU buffer address at +24,
// allocation byte length at +28 (both PPC big endian). This capture NEVER
// trusts the address without the checked ReadGuest, nor logs game bytes.
std::shared_ptr<const OriginalGuestBufferBytes> SourceGpuBuffer(
    uint32_t object,void* guest_base) {
  if(!object || object>UINT32_MAX-32u)return {};
  std::array<std::byte,8> meta{};
  if(!ReadGuest(guest_base,object+24u,meta.data(),meta.size()))return {};
  const auto be32=[](const std::byte* p) {
    return uint32_t(std::to_integer<uint8_t>(p[0]))<<24 |
           uint32_t(std::to_integer<uint8_t>(p[1]))<<16 |
           uint32_t(std::to_integer<uint8_t>(p[2]))<<8 |
           uint32_t(std::to_integer<uint8_t>(p[3]));
  };
  const uint32_t address=be32(meta.data()),bytes=be32(meta.data()+4);
  if(!address || bytes<6 || bytes>(8u<<20) ||
     uint64_t(address)+bytes>uint64_t(UINT32_MAX)+1u)return {};
  // Capture still reads current bytes on every Draw. Deduplicate storage,
  // not memory observations: guest dynamic buffers can change in place.
  return g_original_buffers.Capture(object,address,bytes,ReadGuest,guest_base);
}
bool CaptureEnabled() { return REXCVAR_GET(hulk_eor_xdk_capture); }
std::optional<DeliveredFrame> TakeCompletedPpcFrame() {
  std::lock_guard lock(g_mutex);
  return g_frames_for_native.Take();
}
bool ReplayNextVerifiedPpcFrame(
    std::span<const native::VerifiedXdkResourceWitness> witnesses,
    const native::FrameGeometry& output,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& shaders,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& gpu,
    uint64_t expected_original_frame_sequence,
    bool* reached_gpu) {
  // Preserve a truthful outcome for the caller: some failures happen
  // before ANY Vulkan work and must retain the existing NativeX image.
  if(reached_gpu)*reached_gpu=false;
  // Refuse incomplete engine/host provenance BEFORE consuming the only
  // immutable guest Present snapshot. No guest memory, GPU render targets,
  // or Vulkan swapchain may be touched by a rejected candidate.
  if (!CaptureEnabled() || !REXCVAR_GET(hulk_eor_vulkan_replay) ||
      !output.direct_engine_path_ready ||
      !native::HasFullNativeCompositePixels(
          output.world,output.composite,output.backbuffer) ||
      witnesses.empty() || residents.empty() || !shaders.size() ||
      !effects.EffectCount() ||
      effects.ShaderCount()!=shaders.expected() ||
      reflection.size()!=shaders.expected())
    return false;
  // The caller MUST already be the exclusive render-thread presenter.
  // Inspect the immutable guest Present snapshot without holding g_mutex
  // across timeline checks. An ineligible frame remains available for replay.
  std::shared_ptr<const Frame> preview;
  uint64_t missed=0;
  {
    std::lock_guard lock(g_mutex);
    preview=g_frames_for_native.Peek();
    missed=g_frames_for_native.MissedSinceTake();
  }
  if(!preview)return false;
  const uint64_t sequence=preview->sequence;
  // A frame prepared from the genuine EoR engine must not be paired with
  // another XDK Present that arrived during material/PSO preparation.
  if(expected_original_frame_sequence &&
     sequence!=expected_original_frame_sequence)return false;
  // A worker stalled behind the PPC producer can lose one or more complete
  // mailbox frames. Reject the newest frame if any intervening Present was
  // overwritten, but DISCARD that stale epoch by exact sequence; otherwise
  // MissedSinceTake stays nonzero forever and native replay can never recover.
  // The next independent fully captured frame starts with zero missed loss.
  if(missed) {
    preview.reset(); // release immutable ownership before TakeIfSequence
    std::lock_guard lock(g_mutex);
    (void)g_frames_for_native.TakeIfSequence(sequence);
    return false;
  }
  if(!ValidateOriginalCommandTimeline(*preview))return false;
  try {
    // This eligibility check is read-only: no PSO, RT or GPU writes.
    if(!gpu.CanReplayOriginalXdkTimeline(*preview))return false;
  } catch (...) {return false;}
  if(!CaptureEnabled() || !REXCVAR_GET(hulk_eor_vulkan_replay))
    return false;
  // A concurrent Present can replace the candidate while the GPU backend
  // checks the timeline. Reject stale sequence and any overwritten frames.
  preview.reset();
  std::optional<DeliveredFrame> delivered;
  {
    std::lock_guard lock(g_mutex);
    delivered=g_frames_for_native.TakeIfSequence(sequence);
  }
  if(!delivered || delivered->overwritten_since_take)return false;
  try {
    // All previous checks are read-only. This is the FIRST point at which
    // native GPU work may start. Never classify earlier rejections as
    // device-render failures that suppress the working NativeX Present.
    // Source-only ABI/material/PSO rejection is not a GPU failure.
    // The AOT forwarding backend marks this only at actual BeginScene,
    // BEFORE a possible partial Vulkan attachment bind or driver throw.
    native::ReplayDeliveredResourceScene(*delivered,witnesses,output,
                                          residents,shaders,effects,reflection,gpu,
                                          reached_gpu);
    return true;
  } catch (...) {
    // The caller retains sole-presenter ownership; failure is never a
    // license for a second Xenos/NativeX Present on this swapchain.
    if(delivered->frame.sequence<=5 || delivered->frame.sequence%600==0)
      REXLOG_WARN("[HULK/EOR-REPLAY] frame={} rejected native replay; "
                  "no verified Vulkan image claimed",delivered->frame.sequence);
    return false;
  }
}
void CapturePpcDraw(Kind kind, uint32_t guest, const PPCContext& ctx,
                    uint8_t* guest_base) {
  if (!CaptureEnabled()) return;
  RawDraw draw{
      kind, guest, static_cast<uint32_t>(ctx.lr),
      {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,
       ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32}};
  // The engine shader virtual method was entered with its actual PPC
  // this object, and its first word matched the original USA vtable.
  // Preserve the CALLER of this inner original XDK draw. No new witness
  // flags are inferred: the original mesh/material/effect remains separate.
  if(!g_source_shader_rejected_nesting && g_source_shader_method_depth)
    draw.original_engine_shader_method=
        g_source_shader_method_stack[g_source_shader_method_depth-1];
  // The original source-proven USA 6534 device holds actual VS constants
  // at +1920; capture the 3D World/ViewProj/Previous/Sky registers before
  // the original Draw executes. Do not infer original EoR model identity,
  // camera owner, or SPIR-V UBO layout from these register values.
  if(kind==Kind::DrawIndexedVertices || kind==Kind::DrawVertices) {
    draw.observed_vs_matrices=CaptureUsaVsMatrices(
        ctx.r3.u32,ReadGuest,guest_base);
    if(draw.observed_vs_matrices) {
      if(draw.original_engine_shader_method) {
        auto& owner=*draw.original_engine_shader_method;
        const auto& actual=*draw.observed_vs_matrices;
        if(owner.original_pass_material_context_guest &&
           owner.original_pass_state_one_based &&
           owner.original_pass_vs_xdk_guest_object &&
           owner.original_pass_ps_xdk_guest_object &&
           actual.original_guest_device==ctx.r3.u32 &&
           actual.original_vs_guest_object==
               owner.original_pass_vs_xdk_guest_object &&
           actual.original_ps_guest_object==
               owner.original_pass_ps_xdk_guest_object)
          owner.original_pass_vs_ps_xdk_binding_verified=true;
        if(owner.original_pass_vs_ps_xdk_binding_verified &&
           owner.original_pass_sampler_slots_verified) {
          bool all_source_slots_have_real_guest_textures=true;
          for(uint32_t slot=0u;slot<16u;++slot) {
            if((owner.original_pass_sampler_slot_mask &
                (uint16_t(1u)<<slot)) &&
               !actual.original_texture_guest_objects[slot]) {
              all_source_slots_have_real_guest_textures=false;
              break;
            }
          }
          owner.original_pass_live_texture_objects_present=
              all_source_slots_have_real_guest_textures;
        }
      }
      // Only while explicit diagnostic capture is active. Store all
      // 256 original vertex AND pixel float4 registers, not just camera:
      // later source-proven native SPIR-V material declarations can read
      // exact per-Draw shader parameters without another guest memory read.
      try {
        auto constants=std::make_shared<OriginalShaderRegisters>();
        if(CaptureUsaAllShaderRegisters(ctx.r3.u32,ReadGuest,
                                        guest_base,*constants))
          draw.observed_shader_constants=std::move(constants);
      } catch (const std::bad_alloc&) {
        // Never crash game logic on a diagnostic snapshot allocation.
      }
    }
  }
  // ORIGINAL USA Decal engine PPC (0x8265B420) directly calls XDK
  // DrawIndexedVertices from 0x8265B538 after loading its per-Draw
  // source render-item into r28. The decoded original instructions are:
  //   guest geometry = PPC BE32(r28+0x3C)
  //   SetStreamSource VB descriptor = geometry+0x18
  //   SetIndices      IB descriptor = geometry+0x38
  // Do not apply this ABI to any OTHER shader class/Draw. Verify the
  // live XDK device actually holds these same original guest identities
  // before tagging a real engine->GPU geometry ownership edge.
  if(kind==Kind::DrawIndexedVertices && draw.lr==0x8265B53Cu &&
     draw.original_engine_shader_method && draw.observed_vs_matrices) {
    auto& owner=*draw.original_engine_shader_method;
    const auto& gpu=*draw.observed_vs_matrices;
    if(owner.original_shader_method_guest==0x8265B420u &&
       owner.original_shader_vtable_guest==0x82066504u &&
       owner.original_shader_method_vtable_verified &&
       owner.original_shader_instance_guest==ctx.r31.u32 &&
       owner.source_r4_through_r7[3]==ctx.r27.u32 &&
       ctx.r28.u32 && ctx.r28.u32<=UINT32_MAX-0x3Cu-4u) {
      std::array<std::byte,4> original{};
      if(ReadGuest(guest_base,ctx.r28.u32+0x3Cu,
                   original.data(),original.size())) {
        const uint32_t geometry=
            uint32_t(std::to_integer<uint8_t>(original[0]))<<24 |
            uint32_t(std::to_integer<uint8_t>(original[1]))<<16 |
            uint32_t(std::to_integer<uint8_t>(original[2]))<<8 |
            uint32_t(std::to_integer<uint8_t>(original[3]));
        if(geometry && geometry<=UINT32_MAX-0x38u &&
           gpu.original_vertex_buffer_guest_objects[0]==geometry+0x18u &&
           gpu.original_index_buffer_guest_object==geometry+0x38u &&
           gpu.original_guest_device==draw.gpr[0]) {
          owner.original_decal_render_item_guest=ctx.r28.u32;
          owner.original_decal_geometry_guest=geometry;
          owner.original_decal_xdk_vb_ib_binding_verified=true;
        }
      }
    }
  }
  // Keep buffer revisions and their Draw in the same Present epoch.
  std::lock_guard lock(g_mutex);
  // Only a requested direct Vulkan render performs byte-exact guest
  // resource capture; diagnostics-only XDK observer is unaffected.
  if(REXCVAR_GET(hulk_eor_vulkan_replay) && draw.observed_vs_matrices) {
    try {
      const auto& state=*draw.observed_vs_matrices;
      // Source snapshots are deduplicated by the existing per-Present
      // full-byte checked cache. Repeated Xbox Draws retain identical GPU
      // buffer owners; changed vertex skinning creates a new generation.
      // The per-Present immutable allocation budget is shared by all 16
      // streams. Prioritize the mandatory vertex/index pair BEFORE optional
      // stale bound slots: otherwise an unused slot 15 can exhaust the budget
      // and turn a valid real DrawIndexed into a missing source IB.
      const auto vb0=state.original_vertex_buffer_guest_objects[0];
      if(vb0)
        draw.source_vertex_streams[0]=SourceGpuBuffer(vb0,guest_base);
      draw.source_vb0=draw.source_vertex_streams[0];
      if(kind==Kind::DrawIndexedVertices)
        draw.source_ib=SourceGpuBuffer(
            state.original_index_buffer_guest_object,guest_base);
      for(size_t slot=1;slot<draw.source_vertex_streams.size();++slot) {
        const auto object=state.original_vertex_buffer_guest_objects[slot];
        if(object)
          draw.source_vertex_streams[slot]=SourceGpuBuffer(object,guest_base);
      }
    } catch (const std::bad_alloc&) {
      draw.source_vb0.reset();
      draw.source_ib.reset();
      draw.source_vertex_streams={};
    }
  }
  g_collector.Record(draw);
}
void CapturePpcCommand(CommandKind kind,uint32_t guest,
                       const PPCContext& ctx,uint8_t* guest_base) {
  if(!CaptureEnabled())return;
  RawCommand event{kind,guest,static_cast<uint32_t>(ctx.lr),
                   {ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,
                    ctx.r7.u32,ctx.r8.u32,ctx.r9.u32,ctx.r10.u32},
                   UINT32_MAX};
  // Copy original guest data while ClearF's pointers still refer to
  // the actual command arguments. No guest memory writes or RT guessing.
  if(kind==CommandKind::ClearF)
    event.captured_clear=CaptureOriginalXdkClearF(
        event.gpr,ctx.f1.f64,ReadGuest,guest_base);
  std::lock_guard lock(g_mutex);
  g_collector.RecordCommand(event);
}
void EndPpcHook(native_x_renderer::Call kind) {
  if (kind != native_x_renderer::Call::kPresent || !CaptureEnabled()) return;
  Frame frame;
  std::vector<CallsiteEvidenceRecord> hot;
  uint64_t untracked=0;
  {
    std::lock_guard lock(g_mutex);
    frame = g_collector.Finish();
    g_original_buffers.Reset();
    g_callsite_evidence.Observe(frame);
    if(frame.sequence<=5 || frame.sequence%600==0) {
      hot=g_callsite_evidence.HotCallsites(8);
      untracked=g_callsite_evidence.UntrackedCalls();
    }
  }
  if (frame.sequence <= 5 || frame.sequence % 600 == 0) {
    uint32_t indexed = 0, verts = 0, up = 0;
    uint32_t original_shader_parented=0,original_font_draws=0;
    uint32_t decal_original_geometry_verified=0;
    uint32_t decal_original_material_descriptor_verified=0;
    uint32_t live_original_pass_vs_ps_verified=0;
    uint32_t original_source_sampler_state_verified=0;
    uint32_t original_live_xdk_texture_slot_present=0;
    uint32_t original_engine_render_item_parented=0;
    uint32_t original_engine_item_material_verified=0;
    for (const RawDraw& call : frame.calls) {
      if(const auto& source=call.original_engine_shader_method;source) {
        ++original_shader_parented;
        if(source->original_engine_item_abi_verified &&
           ValidOriginalEorSourceRenderItem(call)) {
          ++original_engine_render_item_parented;
          if(source->original_engine_item_material_source_verified)
            ++original_engine_item_material_verified;
        }
        if(source->original_shader_method_guest==0x8266A0C0u)
          ++original_font_draws;
        if(source->original_decal_xdk_vb_ib_binding_verified &&
           ValidOriginalDecalGeometryBinding(call))
          ++decal_original_geometry_verified;
        if(source->original_decal_item_material_binding_verified &&
           ValidOriginalDecalItemMaterialBinding(call))
          ++decal_original_material_descriptor_verified;
        if(source->original_pass_vs_ps_xdk_binding_verified &&
           ValidOriginalShaderPassStageBinding(call))
          ++live_original_pass_vs_ps_verified;
        if(source->original_pass_sampler_slots_verified &&
           ValidOriginalShaderPassSamplerStateAtDraw(call))
          ++original_source_sampler_state_verified;
        if(source->original_pass_sampler_slot_mask &&
           source->original_pass_live_texture_objects_present &&
           ValidOriginalShaderPassSamplerStateAtDraw(call))
          ++original_live_xdk_texture_slot_present;
      }
      switch (call.kind) {
      case Kind::DrawVertices: ++verts; break;
      case Kind::DrawIndexedVertices: ++indexed; break;
      case Kind::DrawVerticesUP: ++up; break;
      }
    }
    REXLOG_INFO("[HULK/EOR-XDK-PROVENANCE] frame={} real PPC: "
                "indexed={} vertices={} UP={} dropped={} "
                "commands={} dropped_commands={} complete_timeline={} "
                "source_shader_parented={} original_font_draws={} "
                "original_decal_geometry_verified={} "
                "original_decal_material_descriptor_verified={} "
                "eor_pass_stage_vs_ps_xdk_verified={} "
                "eor_pass_sampler_state_verified={} "
                "xdk_sampler_texture_object_presence={} "
                "original_engine_render_item_parented={} "
                "original_engine_item_material_verified={}; "
                "source model hash/texture fetch identity remains unverified",
                frame.sequence,indexed,verts,up,frame.dropped,
                frame.commands.size(),frame.dropped_commands,
                ValidateOriginalCommandTimeline(frame),
                original_shader_parented,original_font_draws,
                decal_original_geometry_verified,
                decal_original_material_descriptor_verified,
                live_original_pass_vs_ps_verified,
                original_source_sampler_state_verified,
                original_live_xdk_texture_slot_present,
                original_engine_render_item_parented,
                original_engine_item_material_verified);
    for (const auto& site: hot) {
      REXLOG_INFO("[HULK/EOR-XDK-CALLSITE] XDK=0x{:08X} LR=0x{:08X} "
                  "kind={} draws={} frames={} first={} last={} "
                  "varying_r3_r10_mask=0x{:02X} nonzero_mask=0x{:02X} "
                  "untracked={} source_defined_guest_bl_caller=0x{:08X}; "
                  "USA XDK entry ABI proven, EoR engine material ABI UNVERIFIED",
                  site.xdk_function,site.caller_lr,static_cast<unsigned>(site.kind),
                  site.recorded_draws,site.frames_seen,site.first_frame,
                  site.last_frame,site.varying_gpr_mask,site.nonzero_gpr_mask,
                  untracked,
                  ProvenUsaXdkDirectBlCaller(site.xdk_function,site.caller_lr));
    }
    // Representative REAL PPC arguments, not invented engine structure
    // offsets. One sample per reported frame is enough for an ABI oracle.
    // Never log original game memory, shader contents or raw GPU buffers.
    const auto sample = std::find_if(frame.calls.begin(), frame.calls.end(),
        [](const RawDraw& call) {
          return call.kind == Kind::DrawIndexedVertices;
        });
    if (sample != frame.calls.end()) {
      const auto& r = sample->gpr;
      REXLOG_INFO("[HULK/EOR-XDK-ABI] indexed_fn=0x{:08X} LR=0x{:08X}"
                  " PPC_r3_r10={:08X},{:08X},{:08X},{:08X},"
                  "{:08X},{:08X},{:08X},{:08X} (uninterpreted)",
                  sample->function, sample->lr, r[0], r[1], r[2], r[3],
                  r[4], r[5], r[6], r[7]);
    }
  }
  // The actual guest Present boundary is the only source of completed
  // original frames. The native Vulkan consumer receives this real,
  // immutable PPC Draw/matrix/object snapshot through TakeCompletedPpcFrame.
  // Keep Xenos as presenter until verified materials and full shader
  // coverage authorize an independent handoff.
  {
    std::lock_guard lock(g_mutex);
    g_frames_for_native.Publish(std::move(frame));
  }
}
bool InstallVerifiedSourceShaderPack(
    std::shared_ptr<const aot::ShaderBank> shaders,
    std::shared_ptr<const aot::EffectCatalog> effects,
    std::shared_ptr<const aot::ReflectionBank> reflection,
    std::shared_ptr<const aot::OriginalXdkShaderSamplers> samplers,
    std::shared_ptr<const aot::OriginalVirtualShaderIdentity> virtual_ids) {
  if(!shaders || !shaders->size() || !shaders->expected() ||
     (samplers && samplers->ShaderCount()!=shaders->expected()) ||
     (virtual_ids && virtual_ids->size()!=shaders->expected()) ||
     !effects || !effects->EffectCount() ||
     effects->ShaderCount()!=shaders->expected() ||
     !reflection || reflection->size()!=shaders->expected())
    return false;
  std::lock_guard lock(g_mutex);
  // A new original ShaderEffects generation must NEVER be attached to
  // an already queued frame from the previous bank's digest identities.
  g_verified_worker_frame.reset();
  g_offline_shader_pack=OfflineShaderPack{
      std::move(shaders),std::move(effects),std::move(reflection),
      std::move(samplers),std::move(virtual_ids)};
  return true;
}
void ClearVerifiedSourceShaderPack() noexcept {
  std::lock_guard lock(g_mutex);
  g_verified_worker_frame.reset();
  g_offline_shader_pack.reset();
}
bool InstallVerifiedWorkerEorFrame(
    std::shared_ptr<const VerifiedWorkerEorFrame> frame) {
  if(!frame || !frame->original_frame_sequence || !frame->device ||
     !frame->output.direct_engine_path_ready ||
     !native::HasFullNativeCompositePixels(
        frame->output.world,frame->output.composite,frame->output.backbuffer) ||
     frame->witnesses.empty() || frame->residents.empty() ||
     !frame->gpu)
    return false;
  for(const auto& model:frame->residents)
    if(!model || !model->original_resource_hash)return false;
  for(const auto& witness:frame->witnesses) {
    const bool same_frame=std::visit([&](const auto& original) {
      return original.native_draw.original_frame_sequence ==
          frame->original_frame_sequence;
    },witness.original_source_submission);
    if(!same_frame)return false;
  }
  std::lock_guard lock(g_mutex);
  if(!g_offline_shader_pack)return false;
  const auto& pack=*g_offline_shader_pack;
  // Never permit a caller's independently constructed ShaderEffects
  // generation to be mixed with the app's actual loaded USA source pack.
  if((frame->shaders && frame->shaders!=pack.shaders) ||
     (frame->effects && frame->effects!=pack.effects) ||
     (frame->reflection && frame->reflection!=pack.reflection) ||
     (frame->samplers && frame->samplers!=pack.samplers) ||
     (frame->virtual_ids && frame->virtual_ids!=pack.virtual_ids))
    return false;
  auto complete=std::make_shared<VerifiedWorkerEorFrame>(*frame);
  // The original USA frame is the sole authority for shader-class parent
  // ownership; no caller may install a stale or invented scope in front of
  // an otherwise valid Vulkan material/mesh witness.
  const auto exact_guest=g_frames_for_native.Peek();
  if(!exact_guest ||
     exact_guest->sequence!=frame->original_frame_sequence ||
     g_frames_for_native.MissedSinceTake()!=0)
    return false;
  try {
    complete->witnesses=native::AttachLiveEorShaderMethodParents(
        *exact_guest,complete->witnesses);
  } catch (...) {
    return false;
  }
  complete->shaders=pack.shaders;
  complete->effects=pack.effects;
  complete->reflection=pack.reflection;
  complete->samplers=pack.samplers;
  complete->virtual_ids=pack.virtual_ids;
  g_verified_worker_frame=std::move(complete);
  return true;
}
bool InstallVerifiedWorkerEorFrameWithResidents(
    std::shared_ptr<const VerifiedWorkerEorFrame> frame,
    const native::VerifiedOriginalResidentLookup& lookup) {
  if(!frame || !frame->original_frame_sequence ||
     frame->witnesses.empty() || !lookup)
    return false;
  std::shared_ptr<const Frame> source;
  {
    std::lock_guard lock(g_mutex);
    source=g_frames_for_native.Peek();
    if(!source || source->sequence!=frame->original_frame_sequence ||
       g_frames_for_native.MissedSinceTake()!=0)
      return false;
  }
  try {
    // Original EoR model/mesh/surface owner must already have been proven
    // by actual engine ABI. This stage uses the existing USA archive stream
    // GPU buffers, not reconstructed or guessed XDK shader state.
    // Pair the ORIGINAL shader class invocation at each XDK Draw with
    // independently authenticated engine mesh/material candidates from
    // this exact Present. This is a real producer of SOURCE-PARENT data,
    // NOT a generator of missing material/pass/texture GPU witnesses.
    auto source_parents=native::AttachLiveEorShaderMethodParents(
        *source,frame->witnesses);
    auto residents=native::RetainVerifiedOriginalFrameResidents(
        *source,source_parents,lookup);
    if(residents.empty())return false;
    auto complete=std::make_shared<VerifiedWorkerEorFrame>(*frame);
    complete->witnesses=std::move(source_parents);
    complete->residents=std::move(residents);
    // A newer XDK Present can replace source while retrieving models.
    // Leave the final exact frame check to TryWorkerEorFrame and replay.
    return InstallVerifiedWorkerEorFrame(std::move(complete));
  } catch (...) {
    // No GPU work started. Keep the established NativeX presentation.
    return false;
  }
}
void ClearVerifiedWorkerEorFrame() noexcept {
  std::lock_guard lock(g_mutex);
  g_verified_worker_frame.reset();
}
EorWorkerStatus GetEorWorkerStatus() noexcept {
  return {g_eor_ready_backbuffers.load(std::memory_order_relaxed),
          g_eor_gpu_failures.load(std::memory_order_relaxed)};
}
native_x_renderer::HulkEorPresentResult TryWorkerEorFrame(
    native_x_renderer::Device& host) noexcept {
  using Result=native_x_renderer::HulkEorPresentResult;
  if(!CaptureEnabled() || !REXCVAR_GET(hulk_eor_vulkan_replay))
    return Result::kNotReady;
  std::shared_ptr<const VerifiedWorkerEorFrame> ready;
  std::shared_ptr<const Frame> source_snapshot;
  uint64_t source_sequence=0;
  {
    std::lock_guard lock(g_mutex);
    ready=g_verified_worker_frame;
    if(!ready)return Result::kNotReady;
    source_snapshot=g_frames_for_native.Peek();
    if(!source_snapshot)return Result::kNotReady;
    source_sequence=source_snapshot->sequence;
    if(g_frames_for_native.MissedSinceTake()) {
      // A dropped mailbox epoch is never permitted to touch Vulkan.
      // Discard it atomically while still under the original Present lock,
      // release the temporary preview first, and let the NEXT frame compete.
      source_snapshot.reset();
      (void)g_frames_for_native.TakeIfSequence(source_sequence);
      return Result::kNotReady;
    }
  }
  if(source_sequence!=ready->original_frame_sequence ||
     ready->device!=&host || !host.swap_chain())
    return Result::kNotReady;
  const auto& desc=host.swap_chain()->GetDesc();
  if(desc.Width!=ready->output.backbuffer.width ||
     desc.Height!=ready->output.backbuffer.height)
    return Result::kNotReady; // resize requires a fresh exact source frame
  std::vector<const model::UploadedOriginalModel*> residents;
  try {
    residents.reserve(ready->residents.size());
    for(const auto& model:ready->residents)residents.push_back(model.get());
    // Verify the ACTUAL original Draw -> guest resource -> native mesh/surface
    // correspondence, paired shader effects and real original SPIR-V BEFORE
    // the worker opens a Vulkan render target. This does not infer an EoR
    // model from LR, a shader hash or an adjacent XDK texture.
    if(!ValidateOriginalCommandTimeline(*source_snapshot) ||
       !ready->gpu->CanReplayOriginalXdkTimeline(*source_snapshot))
      return Result::kNotReady;
    const auto source=native::ResolveCapturedResourceDraws(
        *source_snapshot,ready->witnesses,residents);
    if(source.size()!=source_snapshot->calls.size())
      return Result::kNotReady;
    const auto bound=native::BindVerifiedGeometryDraws(
        source,residents,*ready->effects);
    if(bound.size()!=source.size())return Result::kNotReady;
    for(const auto& draw:bound) {
      if(!ready->shaders->Find(draw.vertex_source_sha256,aot::Stage::Vertex) ||
         !ready->shaders->Find(draw.pixel_source_sha256,aot::Stage::Pixel))
        return Result::kNotReady;
      // Both stages must refer to genuine original USA source program
      // identities when the independently archived HEORSMP1 is loaded.
      // Zero-sampler originals have explicit sentinel records; absence
      // never means that the host may bind an invented blank texture.
      // This validates original source identity without equating an
      // EoR XDK register to an unrelated Xenos fetch constant.
      if(ready->samplers &&
         (!ready->samplers->Find(draw.vertex_source_sha256,
                               aot::Stage::Vertex) ||
          !ready->samplers->Find(draw.pixel_source_sha256,
                               aot::Stage::Pixel)))
        return Result::kNotReady;
    }
  } catch (...) {
    // Pure source/ownership preflight has not touched Diligent.
    // Preserve the normally visible NativeX presentation on rejection.
    return Result::kNotReady;
  }
  // Release the read-only preview so the completed mailbox's exact
  // sequence Take can move the original frame without extra ownership.
  source_snapshot.reset();
  try {
    // This is the REAL sole Vulkan render worker. The final exact-sequence
    // claim is repeated atomically inside ReplayNextVerifiedPpcFrame; a
    // racing newer guest Present can never render with this old EoR owner.
    bool reached_gpu=false;
    const bool submitted=ReplayNextVerifiedPpcFrame(
        ready->witnesses,ready->output,residents,
        *ready->shaders,*ready->effects,*ready->reflection,*ready->gpu,
        source_sequence,&reached_gpu);
    // Safe preflight loss, disabled capture and a newer mailbox frame
    // MUST preserve the visible NativeX frame. Never black-screen a
    // clean rejection; only suppress fallback if native GPU work began.
    if(submitted) {
      g_eor_ready_backbuffers.fetch_add(1,std::memory_order_relaxed);
      return Result::kBackbufferReady;
    }
    if(reached_gpu) {
      g_eor_gpu_failures.fetch_add(1,std::memory_order_relaxed);
      return Result::kFailedAfterGpu;
    }
    return Result::kNotReady;
  } catch (...) {
    // An exception after potential GPU state changes cannot fall back to
    // a competing NativeX composite on the same Vulkan backbuffer.
    g_eor_gpu_failures.fetch_add(1,std::memory_order_relaxed);
    return Result::kFailedAfterGpu;
  }
}
}  // namespace hulk::eor::xdk
