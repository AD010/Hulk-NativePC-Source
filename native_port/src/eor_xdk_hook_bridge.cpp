// Read-only connection to REAL Hulk USA XDK Draw and Present wrappers.
// Never writes guest memory, never bypasses the original NativeX or Xenos.
// This is an input provenance oracle, not a completed Vulkan renderer.
#include "eor_xdk_hook_bridge.h"
#include "eor_original_pause_vm.h"
#include "eor_native_ui_prompts.h"
#include "eor_original_pc_menu.h"
#include "eor_original_item_draw_owner.h"
#include "native_pc_options_store.h"
#include "native_pc_menu_actions.h"
#include <string>
#include "eor_original_havok_step.h"
#include "eor_original_havok_catchup.h"
#include "eor_usa_material_textures.h"
#include "eor_diligent_aot_backend.h"
#ifdef _WIN32
#include "eor_native_frame_executor.h"
#include "eor_native_audio_xaudio2.h"
#include "eor_fmod_live_events.h"
#include "eor_fmod_live_cues.h"
#include "eor_fmod_capture_schedule.h"
#include "eor_reverb_route.h"
#include "eor_fmod_start_trace_bridge.h"
#include <cstdlib>
#include "eor_fmod_project.h"
#endif
#include "eor_diligent_host_output.h"
#include "eor_live_material_producer.h"
#include "eor_live_projection.h"
#include "eor_resource_generation.h"
#include "eor_spirv_clip_position.h"
#include "Buffer.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Fence.h"
#include "RefCntAutoPtr.hpp"
#include "ShaderResourceVariable.h"
#include "eor_original_font_source.h"
#include "eor_native_world_stream.h"
#include "eor_native_archive_source.h"
#include "eor_original_material_candidate_index.h"
#include "device.h"
#include "SwapChain.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <array>
#include <cstring>
#include <deque>
#include <exception>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
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
// Real original GPU vertex/index byte snapshots can be requested separately.
// They remain read-only and are retained only for the current Present.
// This permits source-model identity research with Xenos still presenting,
// without enabling a second Vulkan renderer or guessing material owners.
REXCVAR_DEFINE_BOOL(hulk_eor_xdk_source_mesh_capture, false, "Hulk/Diagnostics",
                    "Read-only original XDK VB/IB bytes; 64 MiB frame cap");
REXCVAR_DEFINE_BOOL(hulk_eor_vulkan_replay, false, "Hulk/Diagnostics",
                    "Opt-in direct EoR Vulkan replay; requires verified sole presenter");
REXCVAR_DEFINE_BOOL(hulk_original_havok_step_probe, false, "Hulk/Diagnostics",
                    "Verify original Havok world and split-step delta; does not modify timing");
REXCVAR_DEFINE_BOOL(hulk_original_havok_catchup, false, "Hulk/Physics",
                    "Experimental bounded catch-up in original 60Hz EoR physics scheduler");
REXCVAR_DEFINE_INT32(hulk_original_subtitles, -1, "Hulk/Original Settings",
                    "Original subtitle preference: -1 inherits the game, 0 off, 1 on").range(-1,1);
REXCVAR_DEFINE_INT32(hulk_original_music_volume,-1,"Hulk/Original Settings",
                    "Original EoR music volume, -1 inherits game").range(-1,100);
REXCVAR_DEFINE_INT32(hulk_original_sfx_volume,-1,"Hulk/Original Settings",
                    "Original EoR effects volume, -1 inherits game").range(-1,100);
REXCVAR_DEFINE_INT32(hulk_original_dialog_volume,-1,"Hulk/Original Settings",
                    "Original EoR dialogue volume, -1 inherits game").range(-1,100);
namespace hulk {
bool GameWindowRequestClose();
void CommitOriginalCameraMenuOption(bool yaw,bool value,uint8_t* base) noexcept;
void RestoreOriginalCameraMenuOptions(uint8_t* base) noexcept;
}
namespace hulk::eor::xdk {
namespace {
std::mutex g_mutex;
std::mutex g_subtitle_mutex;
std::mutex g_original_audio_menu_mutex;
#ifdef _WIN32
std::mutex g_native_fmod_mutex;
std::unique_ptr<audio::NativeXAudio2Mixer> g_native_fmod_mixer;
std::atomic<bool> g_native_fmod_enabled{false};
uint8_t* g_native_fmod_base=nullptr;
uint32_t g_native_fmod_owner=0;
std::vector<audio::LiveFmodEvent> g_native_fmod_events;
std::unique_ptr<fmod::LiveFevCatalog> g_native_fmod_catalog;
std::unique_ptr<Index> g_native_fmod_index;
std::filesystem::path g_native_fmod_project_arc,g_native_fmod_wave_arc;
audio::LiveCueRegistry g_native_fmod_shadow_voices;
audio::FmodStartTraceRegistry g_native_fmod_start_trace;
uint8_t* g_native_fmod_trace_base=nullptr;
bool g_native_fmod_shadow_enabled=false;
struct BoundNativeFmodEvent {
  uint32_t owner=0,project_hash=0;
  audio::LiveFevBinding binding;
  std::vector<audio::FevLayerGainPitch> layer_controls;
};
std::vector<BoundNativeFmodEvent> g_native_fmod_bound_events;
audio::FmodCaptureSchedule g_native_fmod_capture_schedule;
bool g_native_fmod_capture_failed=false;
#endif
std::atomic<uint64_t> g_eor_ready_backbuffers{0};
std::atomic<uint64_t> g_eor_gpu_failures{0};
OriginalEorSourceCoverage g_original_source_coverage; // guarded by g_mutex
std::filesystem::path g_original_game_asset_root; // guarded by g_mutex
std::filesystem::path g_original_material_candidate_index; // guarded by g_mutex
std::filesystem::path g_original_shader_aot_demand_path; // guarded by g_mutex
std::filesystem::path g_last_incremental_spirv_file; // guarded by g_mutex
std::filesystem::file_time_type g_last_incremental_spirv_write{}; // guarded
uint64_t g_last_incremental_spirv_poll=0; // guarded by g_mutex
std::uintmax_t g_last_incremental_spirv_size=0; // guarded
std::set<std::pair<uint32_t,uint32_t>> g_original_shader_aot_requests; // guarded
FrameCapture g_collector;
// Full native replay needs every original shader pair. Once a Draw in
// this epoch is unresolvable, do not copy large VB/IB allocations for
// later Draws which cannot possibly be submitted. Still observe identities.
std::atomic<bool> g_frame_shader_complete{true}; // writes serialized by g_mutex
uint32_t g_frame_heavy_capture_skipped=0; // guarded by g_mutex
// Only full-source-hash-verified pixel policies are retained across draw
// calls. Failed guest reads must be retried when streaming finishes.
aot::OriginalPixelSamplerCache g_pixel_sampling;
aot::OriginalVertexSamplerCache g_vertex_sampling;
XdkCallsiteEvidence g_callsite_evidence;
CompletedFrameMailbox g_frames_for_native;
#ifdef _WIN32
// Protected by g_mutex. Posts at most one deferred UI task; GPU operations
// run after releasing the PPC capture lock.
std::function<void()> g_notify_exclusive_frame;
#endif
std::shared_ptr<const VerifiedWorkerEorFrame> g_verified_worker_frame;
std::shared_ptr<const VerifiedWorkerEorOwnerCatalog> g_worker_owner_catalog;
// This callback belongs to the actual source-verified EoR material/mesh
// loader and executes ONLY on NativeX's shared Vulkan worker, never on
// the original PPC thread. An unavailable source owner returns nullptr.
// All state and generations below are protected by g_mutex.
std::shared_ptr<const VerifiedWorkerEorCatalogFactory> g_worker_catalog_factory;
uint64_t g_worker_catalog_factory_last_attempt=0;
struct OfflineShaderPack {
  std::shared_ptr<const aot::ShaderBank> shaders;
  std::shared_ptr<const aot::EffectCatalog> effects;
  std::shared_ptr<const aot::ReflectionBank> reflection;
  std::shared_ptr<const aot::OriginalXdkShaderSamplers> samplers;
  std::shared_ptr<const aot::OriginalVirtualShaderIdentity> virtual_ids;
};
std::optional<OfflineShaderPack> g_offline_shader_pack;
// An HEORVRT1-only owner enables read-only XDK ShaderObject evidence while
// visible gameplay still uses Xenos; neither SPIR-V nor a Vulkan worker
// needs to exist. Protected by g_mutex like the complete source pack.
std::shared_ptr<const aot::OriginalVirtualShaderIdentity>
    g_original_shader_capture_index;
std::shared_ptr<const aot::EffectCatalog>
    g_original_effect_capture_catalog;
OriginalGuestBufferSnapshots g_original_buffers; // guarded by g_mutex
// Inline font batches are transient and have a separate, bounded budget.
// Large world VB/IB allocations must not consume the original menu/HUD
// glyph payload budget before EndVertices executes near the end of a frame.
OriginalGuestBufferSnapshots g_original_glyph_buffers{8u<<20}; // guarded by g_mutex
// The actual guest EoR shader method and its inner XDK Draw execute on
// the SAME generated PPC thread. A fixed TLS stack retains the source
// method scope without allocations, guest writes or global frame races.
// The genuine engine shader vtable identity is verified before push.
thread_local std::array<OriginalEorShaderMethodAtDraw,16>
    g_source_shader_method_stack{};
thread_local uint8_t g_source_shader_method_depth=0;
thread_local uint32_t g_source_shader_rejected_nesting=0;
// A nested shader or completed render item may change device bindings.
thread_local UsaAppliedMaterial g_applied_item_material{};
OriginalTextureOwnerCache g_original_texture_owners; // guarded by g_mutex
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

// Real EoR method dispatcher. The original UI's StorageOption is an
// already allocated MenuItemTxt with a working selector and a text child.
// On Windows the obsolete console storage action becomes the PC menu.
REX_EXTERN(sub_825B8270);
REX_EXTERN(sub_825B9528);
REX_EXTERN(sub_825B9D98);
REX_EXTERN(sub_8271D340);
REX_EXTERN(sub_8271AA60);
namespace {
#ifdef _WIN32
struct PcMenuGuestScratch {
  PPCContext& original;
  uint8_t* base;
  uint32_t stack=0;
  uint8_t* text=nullptr;
  explicit PcMenuGuestScratch(PPCContext& ctx,uint8_t* b):original(ctx),base(b) {
    const auto sp=ctx.r1.u32;
    // Reserve below the original guest SP; require a real committed stack
    // range for the nested PPC calls and their ABI home/save areas.
    if(!b || sp<0x10000u || sp>=0xe0000000u || (sp&15u) ||
       uint64_t(sp)>UINTPTR_MAX-uintptr_t(b))return;
    const auto begin=uintptr_t(b)+sp-0x8000u;
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(reinterpret_cast<void*>(begin),&info,sizeof(info))!=sizeof(info) ||
       info.State!=MEM_COMMIT || (info.Protect&PAGE_GUARD))return;
    const auto protection=info.Protect&0xffu;
    if(protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
       protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY)return;
    const auto first=uintptr_t(info.BaseAddress);
    if(begin<first || begin-first>info.RegionSize ||
       info.RegionSize-(begin-first)<0x8000u)return;
    stack=sp;text=reinterpret_cast<uint8_t*>(uintptr_t(b)+sp-640u);
  }
  std::optional<uint32_t> Invoke(uint32_t address,std::span<const uint32_t> args) {
    if(!stack || args.size()>6u)return {};
    PPCFunc* fn=nullptr;
    switch(address) {
      case 0x825B8270u:fn=&sub_825B8270;break;
      case 0x825B9528u:fn=&sub_825B9528;break;
      case 0x825B9D98u:fn=&sub_825B9D98;break;
      case 0x8271D340u:fn=&sub_8271D340;break;
      case 0x8271AA60u:fn=&sub_8271AA60;break;
      default:return {};
    }
    PPCContext child=original;
    child.r1.u64=stack-1024u;
    std::array<PPCRegister*,6> regs{&child.r3,&child.r4,&child.r5,&child.r6,&child.r7,&child.r8};
    for(size_t i=0;i<args.size();++i)regs[i]->u64=args[i];
    const auto saved=original.fpscr.getcsr();
    struct RestoreFp {PPCContext& ctx;uint32_t csr;~RestoreFp(){ctx.fpscr.setcsr(csr);}} restore{original,saved};
    (*fn)(child,base);
    return child.r3.u32;
  }
  template<class Read>
  std::optional<bool> Hidden(uint32_t object,Read& read) {
    if(!stack)return {};
    // The original property symbol is initialized by the native engine.
    const auto symbol=ui::WidgetWord(0x82ED42F8u,0,read);
    if(symbol!=ui::WidgetSymbol("Hidden"))return {};
    text[0]=0xff;
    const std::array<uint32_t,3> args{object,*symbol,stack-640u};
    if(Invoke(0x825B9D98u,args)!=1u || text[0]>1u)return {};
    return text[0]!=0;
  }
  // Original dispatcher writes context+20 before VM invocation.
  // Do the same only for an already authenticated, live PC event.
  bool BindOwner(uint32_t context,uint32_t owner) {
    if(!context || (context&3u) || context>0xdfffffe8u || !owner || (owner&3u) ||
       uint64_t(context)+24u>UINTPTR_MAX-uintptr_t(base))return false;
    auto* dst=reinterpret_cast<uint8_t*>(uintptr_t(base)+context+20u);
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(dst,&info,sizeof(info))!=sizeof(info) || info.State!=MEM_COMMIT ||
       (info.Protect&PAGE_GUARD))return false;
    const auto protection=info.Protect&0xffu;
    if(protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
       protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY)return false;
    const auto begin=uintptr_t(info.BaseAddress),at=uintptr_t(dst);
    if(at<begin || at-begin>info.RegionSize || info.RegionSize-(at-begin)<4u)return false;
    for(unsigned i=0;i<4;++i)dst[i]=uint8_t(owner>>(24u-8u*i));
    return true;
  }
  template<class Read>
  bool PaintWide(uint32_t object,std::span<const uint16_t> label,Read& read) {
    if(!stack || label.size()>255u || ui::WidgetWord(object,0,read)!=ui::kOriginalPcTextVtable)return false;
    if(std::find(label.begin(),label.end(),0u)!=label.end())return false;
    for(size_t i=0;i<label.size();++i){text[2*i]=uint8_t(label[i]>>8);text[2*i+1]=uint8_t(label[i]);}
    text[2*label.size()]=text[2*label.size()+1]=0;
    const std::array<uint32_t,2> args{object,stack-640u};
    if(!Invoke(0x8271D340u,args))return false;
    const auto actual=ui::ReadOriginalPcText(object,read);
    return actual && std::equal(label.begin(),label.end(),actual->begin(),actual->end());
  }
  template<class Read>
  bool SetTextHidden(uint32_t object,bool hidden,Read& read) {
    const auto id=ui::ReadOriginalWidget(object,read);
    const auto table=ui::WidgetWord(object,0,read);
    if(!id || !table || !ui::IsOriginalPcTextType(id->definition,*table))return false;
    const auto before=ui::ReadOriginalPcTextHidden(object,read);
    if(!before)return false;
    if(*before==hidden)return true;
    const std::array<uint32_t,3> args{object,uint32_t(hidden),1u};
    return Invoke(0x8271AA60u,args).has_value() && ui::ReadOriginalPcTextHidden(object,read)==hidden;
  }
  template<class Api>
  bool Paint(Api& api,uint32_t row,std::string_view label) {
    if(!stack || label.empty() || label.size()>255u)return false;
    for(char c:label)if(static_cast<unsigned char>(c)<32u ||
                        static_cast<unsigned char>(c)>126u)return false;
    for(size_t i=0;i<label.size();++i){text[2*i]=0;text[2*i+1]=uint8_t(label[i]);}
    text[2*label.size()]=text[2*label.size()+1]=0;
    if(!api.SetRowText(row,stack-640u,label.size()))return false;
    const auto child=api.Find(row,ui::kTextChild);
    if(!child)return false;
    const auto read=[&](uint32_t address,uint8_t* bytes,size_t count){
      return ReadGuest(base,address,reinterpret_cast<std::byte*>(bytes),count);
    };
    const auto actual=ui::ReadOriginalPcText(child->object,read);
    return actual && actual->size()==label.size() &&
        std::equal(actual->begin(),actual->end(),label.begin(),label.end());
  }
};
thread_local ui::OriginalPcStorageMenu g_original_pc_storage_menu;
thread_local ui::OriginalPcNativePageView g_original_pc_page_view;
thread_local bool g_pc_menu_dispatching=false;
#endif
} // namespace

void DispatchOriginalPcMenu(PPCContext& ctx,uint8_t* base,PPCFunc* original,[[maybe_unused]] bool direct_method) {
#ifdef _WIN32
  // Nested source calls and unrelated script events must run normally.
  if(!g_pc_menu_dispatching && base) {
    const auto read=[&](uint32_t a,uint8_t* bytes,size_t n){
      return ReadGuest(base,a,reinterpret_cast<std::byte*>(bytes),n);
    };
    std::optional<ui::PcSettingsEvent> event;
    try {event=ui::CapturePcSettingsEvent(ctx.r3.u32,ctx.r4.u32,read);}catch(...){}
    if(event && direct_method) {
      // Unlike the event wrapper, the original direct method route returns
      // a VM object with ownership. Never synthesize or swallow that result.
      if(event->event!=ui::PcEvent::Enter || !ui::VerifyDirectSettingsMethodAbi(read))
        event.reset();
    }
    if(event) {
      const auto apply=[&](bool entering)->bool {
        struct Scope {Scope(){g_pc_menu_dispatching=true;}~Scope(){g_pc_menu_dispatching=false;}} scope;
        PcMenuGuestScratch scratch(ctx,base);
        const auto reject=[&](const char* reason) {
          REXLOG_WARN("[HULK/ORIGINAL-PC-MENU] root=0x{:08X} event={} rejected: {}",
                      event->root,static_cast<unsigned>(event->event),reason);
          return false;
        };
        if(!scratch.stack)return reject("guest scratch unavailable");
        auto invoke=[&](uint32_t a,std::span<const uint32_t> args){return scratch.Invoke(a,args);};
        ui::OriginalWidgetApi api(read,invoke,true);
        if(!api.Verified())return reject("original FindChild/SetText code mismatch");
        const auto row=api.Find(event->root,ui::WidgetSymbol("StorageOption"));
        // StorageOption has no required Hidden field in either retail page.
        // Missing field is not hidden; exact Selector.Hidden gates activation.
        if(!row || row->definition!=ui::kMenuItemTemplate)
          return reject("original StorageOption row not found");
        if(scratch.Hidden(row->object,read)==std::optional<bool>{true})
          return reject("original StorageOption row hidden");
        auto find=[&](uint32_t p,uint32_t sym){return api.Find(p,sym);};
        auto paint_text=[&](uint32_t obj,std::span<const uint16_t> v){return scratch.PaintWide(obj,v,read);};
        auto hide=[&](uint32_t obj,bool v){return scratch.SetTextHidden(obj,v,read);};
        auto paint=[&](const ui::PcMenuVisual& frame){
          if(!frame.count) {
            if(g_original_pc_page_view.Active() && !g_original_pc_page_view.Restore(event->root,read,paint_text,hide))return false;
            return scratch.Paint(api,row->object,frame.title);
          }
          if(g_original_pc_page_view.Show(event->root,frame,read,find,paint_text,hide))
            return scratch.Paint(api,row->object,frame.title);
          // A complete native row census is required for the expanded view.
          // Keep the existing single-row PC interface when the live layout differs.
          if(g_original_pc_page_view.Active() && !g_original_pc_page_view.Restore(event->root,read,paint_text,hide))return false;
          return scratch.Paint(api,row->object,frame.compact);
        };
        if(entering) {
          if(!g_original_pc_storage_menu.Enter(event->root,row->object,paint))
            return reject("original text setter rejected PC entry");
          REXLOG_INFO("[HULK/ORIGINAL-PC-MENU] native PC settings entry installed root=0x{:08X} row=0x{:08X}",
                      event->root,row->object);
          return true;
        }
        const std::array<uint32_t,2> lookup{event->root,event->context};
        const auto method=scratch.Invoke(0x825B9528u,lookup);
        if(!method || !*method || !scratch.BindOwner(event->context,event->root))
          return reject("original event owner/method unavailable");
        // Some retail StateEnter paths are already executing before the
        // host session exists. Recover on the next authenticated UI event,
        // without stealing focus or consuming the original Back action.
        if(!g_original_pc_storage_menu.Owns(event->root,row->object)) {
          if(event->event==ui::PcEvent::Back || g_original_pc_page_view.Active() ||
             !g_original_pc_storage_menu.Enter(event->root,row->object,paint))return false;
        }
        const auto selector=api.Find(row->object,ui::kSelectorChild);
        const bool focused=selector && scratch.Hidden(selector->object,read)==std::optional<bool>{false};
        return g_original_pc_storage_menu.Handle(event->root,row->object,event->event,focused,
          paint,[](std::string_view key){
            if(key=="window_size") {
              const auto w=rex::cvar::Query<int32_t>("window_width");
              const auto h=rex::cvar::Query<int32_t>("window_height");
              return w>0 && h>0?std::to_string(w)+"x"+std::to_string(h):std::string{"Default"};
            }
            return rex::cvar::GetFlagByName(std::string(key));
          },
          [](std::string_view key,std::string_view value){
            return options::CommitLivePcMenuValue(key,value,[](std::string_view k,std::string_view v){
              return rex::cvar::SetFlagByName(std::string(k),std::string(v));});
          },[] {return hulk::GameWindowRequestClose();});
      };
      if(event->event==ui::PcEvent::Enter) {
        g_original_pc_storage_menu.Leave();
        // The original StateEnter reconstructs page state; restore any
        // still-owned text before it executes, never write to a new owner.
        if(g_original_pc_page_view.Active()) {
          PcMenuGuestScratch scratch(ctx,base);
          if(scratch.stack && ui::VerifyOriginalPcPageTextAbi(read)) {
            auto paint=[&](uint32_t obj,std::span<const uint16_t> v){return scratch.PaintWide(obj,v,read);};
            auto hide=[&](uint32_t obj,bool v){return scratch.SetTextHidden(obj,v,read);};
            try{(void)g_original_pc_page_view.Restore(event->root,read,paint,hide);}catch(...){}
          }
          g_original_pc_page_view.Discard();
        }
        // Suppress only nested PC-menu interception during the original
        // entry routine. Preserve all of the real VM register/return state.
        struct EnterScope {
          EnterScope(){g_pc_menu_dispatching=true;}
          ~EnterScope(){g_pc_menu_dispatching=false;}
        };
        { EnterScope guard; (*original)(ctx,base); }
        if(ui::OriginalSettingsEntrySucceeded(direct_method,ctx.r3.u32))
          try{(void)apply(true);}catch(...){}
        else
          REXLOG_WARN("[HULK/ORIGINAL-PC-MENU] original StateEnter did not succeed: direct={} result=0x{:08X}",
                      direct_method,ctx.r3.u32);
        return;
      }
      try {
        if(apply(false)){ctx.r3.u64=1;return;}
      } catch (...) {g_original_pc_storage_menu.Leave();}
      if(event->event==ui::PcEvent::Back)g_original_pc_storage_menu.Leave();
      (*original)(ctx,base);return;
    }
  }
#endif
  (*original)(ctx,base);
}

std::optional<vm::OriginalCameraMenuChange> CaptureOriginalCameraMenuOption(
    uint32_t method,const PPCContext& ctx,uint8_t* base) noexcept {
  if(!base)return {};
  return vm::CaptureCameraMenuChange(method,static_cast<uint32_t>(ctx.lr),
      ctx.r3.u32,ctx.r31.u32,ctx.r27.u32,ctx.r28.u32,
      [&](uint32_t address,uint8_t* data,size_t size) {
        return ReadGuest(base,address,reinterpret_cast<std::byte*>(data),size);
      });
}
void FinishOriginalCameraMenuOption(
    std::optional<vm::OriginalCameraMenuChange> change,uint8_t* base) noexcept {
  if(!change)return;
  if(!change->subtitles) {
    hulk::CommitOriginalCameraMenuOption(change->yaw,change->value,base);
    return;
  }
  try {
    std::lock_guard lock(g_subtitle_mutex);
    std::byte value{};
    if(!ReadGuest(base,0x82E53B3Eu,&value,1) ||
       std::to_integer<unsigned>(value)!=unsigned(change->value))return;
    REXCVAR_SET(hulk_original_subtitles,change->value?1:0);
    if(!options::SaveOriginalSubtitles(change->value))
      REXLOG_WARN("[HULK/UI] Original subtitle preference applied; PC TOML save failed");
  } catch (...) {}
}

bool StartNativeFmodUpdateService() noexcept {
#ifdef _WIN32
  try {
    std::lock_guard lock(g_native_fmod_mutex);
    if(g_native_fmod_mixer)return true;
    auto mixer=std::make_unique<audio::NativeXAudio2Mixer>();
    if(!mixer->Ready())return false;
    const char* shadow=std::getenv("HULK_EXPERIMENTAL_NATIVE_FMOD_SHADOW");
    g_native_fmod_shadow_enabled=shadow&&shadow[0]=='1'&&shadow[1]=='\0';
    // Never output a duplicate of the original FMOD channel. This explicit
    // diagnostic mode admits native voices but keeps them inaudible.
    if(g_native_fmod_shadow_enabled)mixer->SetMuted(true);
    g_native_fmod_mixer=std::move(mixer);
    g_native_fmod_enabled.store(true,std::memory_order_release);
    return true;
  } catch (...) {}
#endif
  return false;
}
void ConfigureNativeFmodSource(const std::filesystem::path& root) noexcept {
#ifdef _WIN32
  if(root.empty()||!g_native_fmod_enabled.load(std::memory_order_acquire))return;
  try {
    const auto base=root/"DATA"/"XENON";
    const auto xenon=root/"XENON";
    for(const auto& index_path:{base/"USENGLISH"/"INDEX.IND",base/"INDEX.IND",
        xenon/"USENGLISH"/"INDEX.IND",xenon/"INDEX.IND",root/"USENGLISH"/"INDEX.IND",
        root/"INDEX.IND",root/"index(2).ind"}){
      if(!std::filesystem::is_regular_file(index_path))continue;
      try {
        const auto index=Index::LoadPinnedUsa(index_path);
        for(const auto& dir:{index_path.parent_path(),base/"USENGLISH",base,xenon/"USENGLISH",xenon,root}){
          const auto archive=dir/"FMODPROJ.ARC";
          if(!std::filesystem::is_regular_file(archive))continue;
          try {
            auto catalog=fmod::LoadLiveFevCatalog(index,archive);
            if(!catalog)continue;
            std::filesystem::path wave;
            for(const auto& wave_dir:{dir,base/"USENGLISH",base,xenon/"USENGLISH",xenon,root}){
              const auto candidate=wave_dir/"WAVEBANK.ARC";
              if(std::filesystem::is_regular_file(candidate)){wave=candidate;break;}
            }
            auto index_owner=std::make_unique<Index>(index);
            auto project_path=archive;
            std::lock_guard lock(g_native_fmod_mutex);
            if(!g_native_fmod_mixer)return;
            REXLOG_INFO("[HULK/FMOD-NATIVE] loaded {} verified source projects",catalog->projects.size());
            g_native_fmod_mixer->StopAll();
            g_native_fmod_shadow_voices.Clear();
            g_native_fmod_start_trace.Clear();g_native_fmod_trace_base=nullptr;
            g_native_fmod_capture_schedule.Reset();
            g_native_fmod_index=std::move(index_owner);
            g_native_fmod_project_arc=std::move(project_path);
            g_native_fmod_wave_arc=std::move(wave);
            g_native_fmod_catalog=std::move(catalog);
            g_native_fmod_bound_events.clear();
            return;
          } catch (...) {}
        }
      } catch (...) {}
    }
    REXLOG_WARN("[HULK/FMOD-NATIVE] matching pinned USA index/project archive not found");
  } catch (...) {}
#else
  (void)root;
#endif
}
void StopNativeFmodUpdateService() noexcept {
#ifdef _WIN32
  g_native_fmod_enabled.store(false,std::memory_order_release);
  std::lock_guard lock(g_native_fmod_mutex);
  g_native_fmod_mixer.reset();
  g_native_fmod_base=nullptr;g_native_fmod_owner=0;
  g_native_fmod_events.clear();g_native_fmod_bound_events.clear();g_native_fmod_catalog.reset();
  g_native_fmod_index.reset();g_native_fmod_project_arc.clear();g_native_fmod_wave_arc.clear();
  g_native_fmod_shadow_voices.Clear();g_native_fmod_shadow_enabled=false;
  g_native_fmod_start_trace.Clear();g_native_fmod_trace_base=nullptr;
  g_native_fmod_capture_schedule.Reset();g_native_fmod_capture_failed=false;
#endif
}
void TraceFmodSelectionBegin(uint8_t* base,uint32_t child) noexcept {
#ifdef _WIN32
  audio::FmodStartTraceScope::Reset(base);
  if(!base||!child||!g_native_fmod_enabled.load(std::memory_order_acquire))return;
  std::lock_guard lock(g_native_fmod_mutex);
  if(g_native_fmod_shadow_enabled&&g_native_fmod_trace_base==base)g_native_fmod_start_trace.Forget(child);
#else
  (void)base;(void)child;
#endif
}
void TraceFmodSelectionStore(uint8_t* base,uint32_t child,uint32_t parent,uint32_t ordinal) noexcept {
#ifdef _WIN32
  if(!audio::FmodStartTraceScope::Active(base)||!g_native_fmod_enabled.load(std::memory_order_acquire))return;
  audio::FmodStartTraceScope::Reset(base);
  const auto read=[&](uint32_t address,std::byte* data,size_t size){return ReadGuest(base,address,data,size);};
  if(const auto selected=audio::CaptureFmodSelectionStore(child,parent,ordinal,read))
    audio::FmodStartTraceScope::Record(base,*selected);
#else
  (void)base;(void)child;(void)parent;(void)ordinal;
#endif
}
void TraceFmodChannelStart(uint8_t* base,uint32_t child) noexcept {
#ifdef _WIN32
  const auto selected=audio::FmodStartTraceScope::Consume(base);
  if(!base||!child||!g_native_fmod_enabled.load(std::memory_order_acquire))return;
  const auto read=[&](uint32_t address,std::byte* data,size_t size){return ReadGuest(base,address,data,size);};
  const auto channel=selected?audio::ConfirmFmodChannelStart(*selected,child,read):std::nullopt;
  try {
    std::lock_guard lock(g_native_fmod_mutex);
    if(!g_native_fmod_shadow_enabled||!g_native_fmod_mixer||
       (g_native_fmod_trace_base&&g_native_fmod_trace_base!=base))return;
    g_native_fmod_trace_base=base;
    g_native_fmod_start_trace.Forget(child);
    if(channel)(void)g_native_fmod_start_trace.Observe(*selected,*channel);
    // The generated hook runs on the verified successful wave-start path.
    // Even a root/single-variant wave without child selection evidence
    // wakes capture; admission still independently validates its identity.
    const auto flags=audio::FmodTraceWord(child,20,read),handle=audio::FmodTraceWord(child,36,read);
    if(flags&&(*flags&8u)&&handle&&*handle)g_native_fmod_capture_schedule.WaveStarted();
  }catch(...){ /* diagnostic failure cannot affect original playback */ }
#else
  (void)base;(void)child;
#endif
}
std::optional<audio::FmodUpdateStamp> CaptureOriginalFmodUpdate(const PPCContext& ctx,uint8_t* base) noexcept {
#ifdef _WIN32
  if(!base||!g_native_fmod_enabled.load(std::memory_order_acquire))return {};
  const auto read=[&](uint32_t address,std::byte* data,size_t size){return ReadGuest(base,address,data,size);};
  if(!audio::VerifyFmodUpdateBody(read))return {};
  return audio::CaptureFmodUpdateStamp(ctx.r3.u32,read);
#else
  (void)ctx;(void)base;return {};
#endif
}
void FinishOriginalFmodUpdate(std::optional<audio::FmodUpdateStamp> stamp,const PPCContext& ctx,uint8_t* base) noexcept {
#ifdef _WIN32
  if(!stamp||!base||!g_native_fmod_enabled.load(std::memory_order_acquire))return;
  const auto read=[&](uint32_t address,std::byte* data,size_t size){return ReadGuest(base,address,data,size);};
  const auto after=audio::CaptureFmodUpdateStamp(stamp->owner,read);
  if(!after)return;
  const auto delta=audio::FmodUpdateDelta(*stamp,*after,ctx.r3.u32);
  if(!delta)return;
  std::lock_guard lock(g_native_fmod_mutex);
  if(!g_native_fmod_mixer)return;
  if(!g_native_fmod_owner){
    g_native_fmod_owner=stamp->owner;g_native_fmod_base=base;
    REXLOG_INFO("[HULK/FMOD-NATIVE] verified original EventSystem update; native clock connected (original playback retained)");
  }
  if(g_native_fmod_owner!=stamp->owner||g_native_fmod_base!=base)return;
  if(!g_native_fmod_mixer->AdvanceScheduledVoices(*delta))
    REXLOG_WARN("[HULK/FMOD-NATIVE] native voice update rejected");
  // Capture only after the original update has returned successfully.
  // Diagnostic-only mode retains its old cadence; the muted native
  // playback path samples faster and wakes immediately after a wave start.
  const auto now=std::chrono::steady_clock::now();
  if(!g_native_fmod_capture_schedule.Begin(now,g_native_fmod_shadow_enabled))return;
  try {
    auto active=audio::CaptureLiveFmodEvents(stamp->owner,read,
      [&](uint32_t event,uint32_t start,uint32_t child,uint32_t parent,uint32_t channel,uint32_t definition)
          ->std::optional<audio::LiveFmodSelectedOrdinal>{
        if(g_native_fmod_trace_base!=base)return {};
        return g_native_fmod_start_trace.Resolve(event,start,child,parent,channel,definition);
      });
    if(!active){
      if(!g_native_fmod_capture_failed)
        REXLOG_WARN("[HULK/FMOD-NATIVE] live event snapshot rejected; original playback retained");
      g_native_fmod_capture_failed=true;return;
    }
    g_native_fmod_capture_failed=false;
    if(g_native_fmod_trace_base==base)g_native_fmod_start_trace.Retain(
      [&](const audio::FmodStartIdentity& key,uint32_t channel){
        const auto event=std::find_if(active->begin(),active->end(),[&](const auto& e){
          return e.owner==key.event&&e.start_stamp==key.stamp;});
        if(event==active->end())return false;
        if(!event->cue_channels_complete)return true;
        return std::any_of(event->cues.begin(),event->cues.end(),[&](const auto& c){
            return c.instance_owner==key.child&&c.channel==channel;})||
          std::any_of(event->unresolved_channels.begin(),event->unresolved_channels.end(),[&](const auto& c){
            return c.instance_owner==key.child&&c.channel==channel;});
      });
    unsigned reported=0;
    std::vector<BoundNativeFmodEvent> bound;
    for(const auto& event:*active){
      if(g_native_fmod_catalog){
        const auto project=g_native_fmod_catalog->projects.find(event.project);
        if(project!=g_native_fmod_catalog->projects.end()){
          auto binding=audio::BindLiveFevEvent(event,project->first,project->second.table);
          if(binding){
            const auto& definition=project->second.table.events[binding->event_index];
            BoundNativeFmodEvent item{event.owner,project->second.hash,std::move(*binding),{}};
            for(uint32_t layer=0;layer<definition.layer_records.size();++layer){
              const auto controls=audio::EvaluateFevLayerGainPitch(definition,layer,item.binding.physical_values);
              if(!controls)break;
              item.layer_controls.push_back(*controls);
            }
            if(item.layer_controls.size()==definition.layer_records.size())bound.push_back(std::move(item));
          }
        }
      }
      const auto same=std::find_if(g_native_fmod_events.begin(),g_native_fmod_events.end(),
        [&](const auto& old){return old.owner==event.owner&&old.project_owner==event.project_owner&&
           old.project==event.project&&old.group==event.group&&old.name==event.name;});
      if(same==g_native_fmod_events.end()&&reported++<16u)
        REXLOG_INFO("[HULK/FMOD-NATIVE] active event {} / {} / {} parameters={}",
          event.project,event.group,event.name,event.parameters.size());
    }
    if(bound.size()!=g_native_fmod_bound_events.size())
      REXLOG_INFO("[HULK/FMOD-NATIVE] bound {} of {} observed active event roots to original FEV",bound.size(),active->size());
    if(g_native_fmod_shadow_enabled&&g_native_fmod_catalog&&g_native_fmod_index&&!g_native_fmod_wave_arc.empty()){
      // Admit a bounded polyphonic batch per shadow update. This remains
      // a synchronous decode path, not a sample-accurate event scheduler.
      g_native_fmod_shadow_voices.Reconcile(*active,
        [&](const audio::LiveFmodEvent& event,const audio::LiveFmodCue& cue)->uint64_t{
          const auto project=g_native_fmod_catalog->projects.find(event.project);
          if(project==g_native_fmod_catalog->projects.end())return 0;
          const auto binding=audio::BindLiveFevEvent(event,project->first,project->second.table);
          if(!binding)return 0;
          const auto offset=audio::ResolveLiveFmodCue(project->second.table,*binding,cue);
          if(!offset)return 0;
          const auto& definition=project->second.table.events[binding->event_index];
          audio::VoiceId id=0;
          bool accepted=false;
          // Source waveform route checks the archive/index pairing again.
          // Use current observed parameters, not a fabricated future curve.
          const auto spatial=cue.spatial?audio::ResolveLiveFmodSpatial(*cue.spatial):std::nullopt;
          if((cue.spatial&&!spatial)||(cue.audibility&&cue.audibility->spatial&&!spatial))return 0; // never reinterpret an invalid 3D snapshot as 2D
          audio::VoiceOptions options;
          options.spatial=spatial;
          if(spatial&&cue.spatial->reverb_sends){
            if(const auto wet=audio::ResolveNativeRoomRoutes(*cue.spatial->reverb_sends))
              options.room_routes=*wet;
          }
          options.reserve_live_pitch=true;
          options.start_paused=cue.audibility&&cue.audibility->paused.value_or(false);
          if(const auto controls=audio::ResolveLiveMutableControls(definition,cue.layer,binding->physical_values,spatial.has_value())){
            options.frequency_ratio=controls->ratio;
            options.stereo_pan=controls->pan;
            options.spatial_level=controls->pan3d_level;
            accepted=g_native_fmod_mixer->PlayProjectWaveform(audio::Bus::Sfx,*g_native_fmod_index,
                g_native_fmod_project_arc,g_native_fmod_wave_arc,project->second.hash,*offset,audio::ResolveLiveShadowGain(cue,controls->gain),&id,options);
          }else{
            options.live_fev_dsp=true;
            const std::array<audio::FevParameterSnapshot,1> snapshots{{{0,std::span<const float>(binding->physical_values)}}};
            accepted=g_native_fmod_mixer->PlayEventVariantWithFevDsp(audio::Bus::Sfx,*g_native_fmod_index,
                g_native_fmod_project_arc,g_native_fmod_wave_arc,project->second.hash,event.group,event.name,
                cue.layer,cue.instance,cue.variant,{},snapshots,1.f,&id,options);
          }
          REXLOG_INFO("[HULK/FMOD-NATIVE] muted shadow cue {} / {} variant={} admitted={} id={} source_audibility={}",
              event.project,cue.definition,cue.variant,accepted,id,cue.audibility?cue.audibility->combined:-1.f);
          if(accepted&&cue.spatial&&cue.spatial->acoustics){
            const auto& a=*cue.spatial->acoustics;
            REXLOG_INFO("[HULK/FMOD-NATIVE] acoustic source direct={} reflected={} reverb_sends={}",
                a.direct,a.reflected,cue.spatial->reverb_sends?static_cast<int>(cue.spatial->reverb_sends->count):-1);
          }
          return accepted?id:0;
        },
        [&](uint64_t id){(void)g_native_fmod_mixer->StopVoice(id);},
        audio::FmodCaptureSchedule::ShadowStartBudget());
      g_native_fmod_shadow_voices.UpdateActive(*active,
        [&](uint64_t id,const audio::LiveFmodEvent& event,const audio::LiveFmodCue& cue){
          const auto project=g_native_fmod_catalog->projects.find(event.project);
          if(project==g_native_fmod_catalog->projects.end())return false;
          const auto binding=audio::BindLiveFevEvent(event,project->first,project->second.table);
          if(!binding||!audio::ResolveLiveFmodCue(project->second.table,*binding,cue))return false;
          const auto& definition=project->second.table.events[binding->event_index];
          if(cue.layer>=definition.layer_records.size())return false;
          const auto spatial=cue.spatial?audio::ResolveLiveFmodSpatial(*cue.spatial):std::nullopt;
          if((cue.spatial&&!spatial)||(cue.audibility&&cue.audibility->spatial&&!spatial))return false;
          if(spatial&&cue.spatial->reverb_sends){
            // Missing/invalid room evidence does not turn the dry voice off.
            // On actual host route failure the registry retires the shadow.
            if(const auto wet=audio::ResolveNativeRoomRoutes(*cue.spatial->reverb_sends))
              if(!g_native_fmod_mixer->SetVoiceRoomRoutes(id,*wet))return false;
          }else if(!spatial&&!g_native_fmod_mixer->SetVoiceRoomRoutes(id,{}))return false;
          if(cue.audibility&&cue.audibility->paused&&
             !g_native_fmod_mixer->PauseVoice(id,*cue.audibility->paused))return false;
          // Live DSP updates the next unqueued PCM block, retaining its
          // original filter history and existing XAudio2 source voice.
          const auto controls=audio::ResolveLiveMutableControls(definition,cue.layer,binding->physical_values,spatial.has_value());
          if(!controls){
            const auto snapshot=audio::EvaluateFevLayerGainPitch(definition,cue.layer,binding->physical_values,spatial.has_value());
            if(!snapshot||snapshot->unhandled)return false;
            return g_native_fmod_mixer->SetVoiceLiveFevDsp(id,definition,cue.layer,binding->physical_values,spatial.has_value())&&
                g_native_fmod_mixer->SetVoiceLiveFevGainPitch(id,snapshot->values.gain,snapshot->values.ratio)&&
                g_native_fmod_mixer->SetVoiceSpatialMix(id,spatial,snapshot->values.pan,snapshot->values.pan3d_level);
          }
          return g_native_fmod_mixer->SetVoiceGain(id,audio::ResolveLiveShadowGain(cue,controls->gain))&&
              g_native_fmod_mixer->SetVoiceFrequencyRatio(id,controls->ratio)&&
              g_native_fmod_mixer->SetVoiceSpatialMix(id,spatial,controls->pan,controls->pan3d_level);
        },
        [&](uint64_t id){(void)g_native_fmod_mixer->StopVoice(id);});
    }
    g_native_fmod_bound_events=std::move(bound);
    g_native_fmod_events=std::move(*active);
  } catch (...) {
    // Diagnostic allocation or read failure must not affect original audio.
  }
#else
  (void)stamp;(void)ctx;(void)base;
#endif
}
std::optional<vm::AudioMenuChange> CaptureOriginalAudioMenuOption(
    uint32_t method,const PPCContext& ctx,uint8_t* base) noexcept {
  if(!base)return {};
  return vm::CaptureAudioMenuChange(method,static_cast<uint32_t>(ctx.lr),
      ctx.r3.u32,ctx.r31.u32,ctx.r27.u32,ctx.r28.u32,
      [&](uint32_t address,uint8_t* data,size_t size) {
        return ReadGuest(base,address,reinterpret_cast<std::byte*>(data),size);
      });
}
void FinishOriginalAudioMenuOption(std::optional<vm::AudioMenuChange> change,
    uint8_t* base) noexcept {
#ifdef _WIN32
  if(!base || !change)return;
  try {
    std::lock_guard lock(g_original_audio_menu_mutex);
    const auto read=[&](uint32_t address,uint8_t* data,size_t size) {
      return ReadGuest(base,address,reinterpret_cast<std::byte*>(data),size);
    };
    if(change->channel==vm::AudioChannel::Lock) {
      // The VM may recreate its audio page or reset active volumes while
      // retaining the same guest address space. Restore user preferences
      // at every authenticated lock transition, not just the first one.
      const std::array<int32_t,3> prefs{
          REXCVAR_GET(hulk_original_music_volume),REXCVAR_GET(hulk_original_sfx_volume),
          REXCVAR_GET(hulk_original_dialog_volume)};
      const auto plan=vm::PlanDeferredVolumeRestore(*change,prefs,read);
      if(!plan)return;
      if(plan->after!=plan->before) {
        constexpr auto address=vm::kOriginalDeferredVolume;
        if(address>UINTPTR_MAX-uintptr_t(base))return;
        auto* dst=reinterpret_cast<uint8_t*>(uintptr_t(base)+address);
        MEMORY_BASIC_INFORMATION info{};
        if(VirtualQuery(dst,&info,sizeof(info))!=sizeof(info) ||
           info.State!=MEM_COMMIT || (info.Protect&PAGE_GUARD))return;
        const auto protection=info.Protect&0xffu;
        if(protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
           protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY)return;
        const auto start=uintptr_t(info.BaseAddress),at=uintptr_t(dst);
        if(at<start || at-start>info.RegionSize || info.RegionSize-(at-start)<12u ||
           std::memcmp(dst,plan->before.data(),12)!=0 ||
           vm::ReadVolumeLock(read)!=std::optional<bool>{true})return;
        std::memcpy(dst,plan->after.data(),12);
      }
      return;
    }
    if(!vm::OriginalVolumeApplied(*change,read))return;
    const auto value=static_cast<int32_t>(change->value);
    const char* key=nullptr;
    int32_t previous=-1;
    switch(change->channel) {
      case vm::AudioChannel::Music:
        key="hulk_original_music_volume";previous=REXCVAR_GET(hulk_original_music_volume);
        REXCVAR_SET(hulk_original_music_volume,value);break;
      case vm::AudioChannel::Sfx:
        key="hulk_original_sfx_volume";previous=REXCVAR_GET(hulk_original_sfx_volume);
        REXCVAR_SET(hulk_original_sfx_volume,value);break;
      case vm::AudioChannel::Dialog:
        key="hulk_original_dialog_volume";previous=REXCVAR_GET(hulk_original_dialog_volume);
        REXCVAR_SET(hulk_original_dialog_volume,value);break;
      default:return;
    }
    // Source slider callbacks may repeat without any change; no disk I/O
    // for the unchanged original value. Original mixer remains authoritative.
    if(previous!=value && !options::SaveOriginalAudioVolume(key,change->value))
      REXLOG_WARN("[HULK/UI] Original EoR volume changed; PC TOML save failed");
  } catch (...) {}
#else
  (void)change;(void)base;
#endif
}
std::optional<vm::CameraSpeedEvent> CaptureOriginalCameraSpeed(uint32_t method,const PPCContext& ctx,uint8_t* base) noexcept {
  if(!base)return {};
  return vm::CaptureCameraSpeed(method,static_cast<uint32_t>(ctx.lr),ctx.r3.u32,
      ctx.r31.u32,ctx.r27.u32,ctx.r28.u32,
      [&](uint32_t a,uint8_t* b,size_t n){return ReadGuest(base,a,reinterpret_cast<std::byte*>(b),n);});
}
void FinishOriginalCameraSpeed(std::optional<vm::CameraSpeedEvent> event,uint8_t* base) noexcept {
#ifdef _WIN32
  if(!event || !base)return;
  try {
    const auto& e=*event;
    const auto read=[&](uint32_t a,uint8_t* b,size_t n){return ReadGuest(base,a,reinterpret_cast<std::byte*>(b),n);};
    const std::string key=e.method->horizontal?"hulk_mouse_horizontal_multiplier":"hulk_mouse_vertical_multiplier";
    const auto current=rex::cvar::GetFlagByName(key);
    if(e.method->setter) {
      if(!vm::CameraSpeedApplied(e,read))return;
      const auto value=std::to_string(vm::CameraSpeedMultiplier(e.value));
      if(current==value)return;
      const auto result=options::CommitLivePcMenuValue(key,value,
          [](std::string_view k,std::string_view v){return rex::cvar::SetFlagByName(std::string(k),std::string(v));});
      if(!result.persisted || !result.live_applied)
        REXLOG_WARN("[HULK/UI] Original camera speed applied; PC setting update failed");
      return;
    }
    // Original getter has already written its own VM return object.
    // Replace only that verified output; do not multiply guest camera globals.
    const auto previous=vm::CameraSpeedWord(e.output,0,read);
    if(!previous || *previous>300)return;
    const auto desired=vm::CameraSpeedSlider(std::stod(current));
    if(*previous==desired)return;
    if(e.output>UINTPTR_MAX-uintptr_t(base))return;
    auto* dst=reinterpret_cast<uint8_t*>(uintptr_t(base)+e.output);
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(dst,&info,sizeof(info))!=sizeof(info) || info.State!=MEM_COMMIT ||
       (info.Protect&PAGE_GUARD))return;
    const auto protection=info.Protect&0xffu;
    if(protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
       protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY)return;
    const auto start=uintptr_t(info.BaseAddress),at=uintptr_t(dst);
    if(at<start || at-start>info.RegionSize || info.RegionSize-(at-start)<4u)return;
    if(vm::CameraSpeedWord(e.output,0,read)!=previous)return;
    std::array<uint8_t,4> bytes{};
    for(unsigned i=0;i<4;++i)bytes[i]=uint8_t(desired>>(24-8*i));
    std::memcpy(dst,bytes.data(),4);
  }catch(...){}
#else
  (void)event;(void)base;
#endif
}
void RestoreOriginalCameraGetter(uint32_t method,const PPCContext& ctx,uint8_t* base) noexcept {
  if(!base)return;
  if(vm::VerifyCameraGetter(method,static_cast<uint32_t>(ctx.lr),ctx.r3.u32,
      ctx.r31.u32,ctx.r27.u32,ctx.r28.u32,
      [&](uint32_t address,uint8_t* data,size_t size) {
        return ReadGuest(base,address,reinterpret_cast<std::byte*>(data),size);
      }))
    hulk::RestoreOriginalCameraMenuOptions(base);
}

void RestoreOriginalSubtitleOption(const PPCContext& ctx,uint8_t* base) noexcept {
#ifdef _WIN32
  if(!base)return;
  try {
    std::lock_guard lock(g_subtitle_mutex);
    const auto desired=REXCVAR_GET(hulk_original_subtitles);
    if(desired<0 || desired>1)return;
    if(!vm::VerifySubtitleGetter(static_cast<uint32_t>(ctx.lr),ctx.r3.u32,
        ctx.r31.u32,ctx.r27.u32,ctx.r28.u32,
        [&](uint32_t address,uint8_t* data,size_t size) {
          return ReadGuest(base,address,reinterpret_cast<std::byte*>(data),size);
        }))return;
    constexpr uint32_t address=0x82E53B3Eu;
    if(address>UINTPTR_MAX-uintptr_t(base))return;
    auto* dst=reinterpret_cast<uint8_t*>(uintptr_t(base)+address);
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(dst,&info,sizeof(info))!=sizeof(info) ||
       info.State!=MEM_COMMIT || (info.Protect&PAGE_GUARD))return;
    const auto protection=info.Protect&0xffu;
    if(protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
       protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY)return;
    std::byte previous{};
    if(!ReadGuest(base,address,&previous,1) || std::to_integer<unsigned>(previous)>1u ||
       *dst!=std::to_integer<uint8_t>(previous))return;
    // Reopening the original page after a game reset must still reflect
    // the host preference. Avoid a write when the original value agrees.
    if(*dst!=static_cast<uint8_t>(desired))
      *dst=static_cast<uint8_t>(desired);
  } catch (...) {}
#else
  (void)ctx;(void)base;
#endif
}

void ApplyOriginalHavokCatchup(const PPCContext& ctx,uint8_t* base) noexcept {
#ifdef _WIN32
  // Called from the original floor routine. All other callers must leave
  // even the TLS debt untouched. The scheduler owns r31 at this BL.
  if(static_cast<uint32_t>(ctx.lr)!=havok::kOriginalSchedulerFloorReturn)return;
  struct Debt {
    uint8_t* base=nullptr;
    uint32_t owner=0,world=0,simulation=0,scheduler=0,ticks=0;
    float time=0;
  };
  // The original scheduler has finished its pre-step callbacks and read
  // elapsed/step. It has not loaded owner+24 or woken any Havok worker.
  // Inject only into that accumulator at this exact source call boundary.
  static thread_local Debt debt;
  if(!base || !REXCVAR_GET(hulk_original_havok_catchup)) {debt={};return;}
  try {
    const auto clock=havok::CaptureOuterClock(ctx.r31.u32,
        [&](uint32_t address,uint8_t* data,size_t size) {
          return ReadGuest(base,address,reinterpret_cast<std::byte*>(data),size);
        });
    if(!clock || !havok::MatchesOriginalSchedulerFloorInput(
        static_cast<uint32_t>(ctx.lr),ctx.f1.f64,clock->elapsed,clock->step))
      {debt={};return;}
    if(debt.base!=base || debt.owner!=clock->owner || debt.world!=clock->world ||
       debt.simulation!=clock->simulation || debt.scheduler!=clock->scheduler ||
       clock->simulation_time<debt.time)debt={};
    const auto plan=havok::PlanOriginalCatchup(clock->elapsed,clock->step,
                                              clock->accumulator,debt.ticks);
    if(!plan) {debt={};return;}
    if(plan->extra) {
      // Verified writable aligned 4-byte guest accumulator. Never change
      // the original global delta, simulation rate or scheduler job fields.
      const uint64_t address=uint64_t(clock->owner)+24u;
      const uint64_t offset=address+(address>=0xE0000000u?4096u:0u);
      if(address>UINT32_MAX-3u || (address&3u) || offset>UINTPTR_MAX-uintptr_t(base))
        {debt={};return;}
      const auto at=uintptr_t(base)+uintptr_t(offset);
      MEMORY_BASIC_INFORMATION info{};
      if(VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof(info))!=sizeof(info) ||
         info.State!=MEM_COMMIT || (info.Protect&PAGE_GUARD)) {debt={};return;}
      const auto protection=info.Protect&0xffu;
      if(protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
         protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY)
        {debt={};return;}
      const auto start=uintptr_t(info.BaseAddress);
      if(at<start || at-start>info.RegionSize || info.RegionSize-(at-start)<4u)
        {debt={};return;}
      auto raw=std::bit_cast<uint32_t>(plan->accumulator);
      const auto expected=std::bit_cast<uint32_t>(clock->accumulator);
      std::array<uint8_t,4> previous{},next{};
      for(unsigned i=0;i<4;++i) {
        previous[i]=uint8_t(expected>>(24-8*i));next[i]=uint8_t(raw>>(24-8*i));
      }
      auto* dst=reinterpret_cast<uint8_t*>(at);
      if(std::memcmp(dst,previous.data(),4)!=0) {debt={};return;}
      std::memcpy(dst,next.data(),4);
    }
    debt={base,clock->owner,clock->world,clock->simulation,clock->scheduler,
          plan->debt,clock->simulation_time};
  } catch (...) {debt={};}
#else
  (void)ctx;(void)base;
#endif
}

void ObserveOriginalHavokStep(uint32_t method,const PPCContext& ctx,uint8_t* base) noexcept {
  if(!base || !REXCVAR_GET(hulk_original_havok_step_probe))return;
  try {
    const auto step=havok::CaptureStep(method,static_cast<uint32_t>(ctx.lr),
        ctx.r3.u32,ctx.r31.u32,ctx.f1.f64,
        [&](uint32_t address,uint8_t* data,size_t size) {
          return ReadGuest(base,address,reinterpret_cast<std::byte*>(data),size);
        });
    static std::atomic<uint64_t> verified{0},rejected{0};
    if(!step) {
      const auto n=rejected.fetch_add(1,std::memory_order_relaxed)+1;
      if(n<=8 || n%600==0)
        REXLOG_WARN("[HULK/HAVOK] rejected source step ABI method=0x{:08X} lr=0x{:08X} count={}",
                    method,static_cast<uint32_t>(ctx.lr),n);
      return;
    }
    const auto n=verified.fetch_add(1,std::memory_order_relaxed)+1;
    if(n<=16 || n%300==0)
      REXLOG_INFO("[HULK/HAVOK] verified step={} split={} simulation={} delta={} current={} psi={} previous_delta={}",
          n,step->split,static_cast<unsigned>(step->kind),step->seconds,
          step->current_time,step->psi_time,step->previous_delta);
  } catch (...) {}
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
  const auto page=vm::ResolveNativeOwner(
      call_lr,original_ctx.r3.u32,original_ctx.r31.u32,
      original_ctx.r27.u32,original_ctx.r28.u32,
      [&](uint32_t address,uint8_t* bytes,size_t length) {
        return ReadGuest(original_guest_base,address,
            reinterpret_cast<std::byte*>(bytes),length);
      });
  // Only the interpreter's live owner can name a Pause page. Do not
  // mutate the shared state_r return or confuse it with a menu action.
  const char* page_name=page.page==vm::PausePage::Settings?"UIPage_Pause_Settings":
      page.page==vm::PausePage::GameSettings?"UIPage_Pause_GameSettings":
      page.page==vm::PausePage::AudioSettings?"UIPage_Pause_AudioSettings":
      page.page==vm::PausePage::CameraSettings?"UIPage_Pause_CameraSettings":
      page.page==vm::PausePage::FrontendSettings?"UIWidget_Frontend_Settings":
      "PAGE-OWNER UNKNOWN";
  static std::atomic<uint64_t> calls{0};
  const auto n=calls.fetch_add(1,std::memory_order_relaxed)+1u;
  // Bound log cost and preserve only original PPC virtual addresses.
  // This common VM getter/setter must NEVER be promoted to a native
  // Windows menu event merely because ChangeState_r calls it.
  if(n<=8u || (n%8192u)==0u || page.page!=vm::PausePage::Unknown)
    REXLOG_INFO("[HULK/EOR-VM-STATE-R] calls={} original_native=0x{:08X} "
                "guest_LR=0x{:08X} VM_r3=0x{:08X} frame=0x{:08X} "
                "input_owner=0x{:08X} output_owner=0x{:08X} "
                "vm_state_before=0x{:08X} vm_state_requested=0x{:08X} "
                "complete_ppc_source_chain={}; "
                "owner_page={} owner_object=0x{:08X} owner_definition=0x{:08X} "
                "owner_key=0x{:08X}; not native Pause Settings event",
                n,original_method_guest,call_lr,original_ctx.r3.u32,
                vm_frame.value_or(0u),input.value_or(0u),
                output.value_or(0u),previous.value_or(0u),
                requested.value_or(0u),
                static_cast<bool>(requested && previous),page_name,page.object,page.definition,page.key);
}

void ObserveOriginalEorShaderPass(
    uint32_t original_method_guest,const PPCContext& original_ctx,
    uint8_t* original_guest_base) noexcept {
  if(!CaptureEnabled() || original_method_guest!=0x825F84C0u ||
     !original_guest_base || !g_source_shader_method_depth ||
     g_source_shader_rejected_nesting)return;
  g_applied_item_material={};
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
  scope.original_pass_state_record={};
  scope.original_pass_state_record_captured=false;
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
  if(start>UINT32_MAX-91u)return;
  // The original material pass selector 0x825F84C0 reads byte
  // slot indexes at original 92-byte state record +84 (VS), +85 (PS),
  // multiplying each by 8 and reading the stage table entry's first
  // BE32 guest shader object ID. This is independently confirmed by
  // 0x82675C70/0x82675D20 and XDK 0x82874390/0x82874090.
  // Read the actual original 92-byte shader-state record starting at
  // +80 (BE16 sampler table index), +84/+85 VS/PS stage indexes,
  // +87 count of 4-byte original sampler-state records. Only original
  // source data is read; no host bindings or textures are invented.
  std::array<std::byte,92> original_state{};
  if(!ReadGuest(original_guest_base,static_cast<uint32_t>(start),
                original_state.data(),original_state.size()))return;
  const auto* shader_slots=original_state.data()+80u;
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
  scope.original_pass_state_record=original_state;
  scope.original_pass_state_record_captured=true;
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
  g_applied_item_material=ReadUsaAppliedMaterial(original_ctx.r3.u32,original_ctx.r4.u32,
                                                 ReadGuest,original_guest_base);
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
  // Nested original shader classes may temporarily replace the current
  // material binding. Remember the parent's exact descriptor so that its
  // later Draw can still be authenticated against CURRENT guest memory.
  prior_applied_material_=g_applied_item_material;
  prior_shader_depth_=g_source_shader_method_depth;
  prior_rejected_nesting_=g_source_shader_rejected_nesting;
  if(prior_shader_depth_)
    prior_shader_instance_=g_source_shader_method_stack[prior_shader_depth_-1].original_shader_instance_guest;
  touched_material_=true;
  g_applied_item_material={};
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
  if(!touched_material_)return;
  if(suppressed_ && g_source_shader_rejected_nesting)
    --g_source_shader_rejected_nesting;
  if(active_ && g_source_shader_method_depth)
    --g_source_shader_method_depth;
  // Do not restore a material into a different guest shader owner after
  // malformed or unbalanced hooks. The live Draw still compares the
  // restored descriptor to original guest bytes and current XDK slots.
  const bool same_parent= !prior_shader_depth_ ||
      (g_source_shader_method_depth==prior_shader_depth_ &&
       g_source_shader_method_stack[prior_shader_depth_-1].original_shader_instance_guest==prior_shader_instance_);
  if(same_parent && g_source_shader_method_depth==prior_shader_depth_ &&
     g_source_shader_rejected_nesting==prior_rejected_nesting_)
    g_applied_item_material=prior_applied_material_;
  else g_applied_item_material={};
}

ScopedOriginalEorRenderItem::ScopedOriginalEorRenderItem(
    uint32_t original_method_guest,const PPCContext& original_ctx,
    uint8_t* original_guest_base) noexcept {
  const bool actual_draw=IsUsaItemDrawEntry(original_method_guest);
  if(!CaptureEnabled() || !g_source_shader_method_depth ||
     g_source_shader_rejected_nesting ||
     (!actual_draw && !ProvenUsaEorRenderItemMethodEntry(original_method_guest)))
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
  prior_applied_material_=g_applied_item_material;
  touched_=true;
  // Prevent one sibling item from inheriting a prior item's texture
  // descriptor before its own original source material setter executes.
  g_applied_item_material={};
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
  if(actual_draw) {
    if(!original_guest_base)return;
    auto read=[&](uint32_t address,uint8_t* data,size_t bytes){
      return ReadGuest(original_guest_base,address,reinterpret_cast<std::byte*>(data),bytes);
    };
    struct VerifiedCode {uint8_t* base=nullptr;bool valid=false;};
    static thread_local VerifiedCode code;
    if(code.base!=original_guest_base) {
      code={original_guest_base,VerifyUsaItemDrawCode(read)};
    }
    if(!code.valid)return;
    const UsaItemDrawRegisters regs{
      original_method_guest,static_cast<uint32_t>(original_ctx.lr),
      original_ctx.r3.u32,original_ctx.r4.u32,original_ctx.r31.u32,
      original_ctx.r28.u32,original_ctx.r23.u32,original_ctx.r27.u32,
      original_ctx.r30.u32,original_ctx.r24.u32,original_ctx.r19.u32};
    const auto owner=ReadUsaItemDrawOwner(regs,read);
    if(!owner || !parent.original_shader_method_vtable_verified ||
       regs.shader!=parent.original_shader_instance_guest ||
       owner->shader_vtable!=parent.original_shader_vtable_guest ||
       !ProvenUsaEorRenderItemMethodVtablePair(owner->method,owner->shader_vtable))return;
    parent.original_engine_item_method_guest=owner->method;
    parent.original_engine_render_item_guest=regs.item;
    parent.original_engine_render_item_context_guest=regs.context;
    parent.original_engine_render_item_vtable_guest=owner->item_vtable;
    parent.original_engine_render_item_material_guest=owner->material;
    parent.original_engine_item_material_source_verified=owner->material!=0u;
    parent.original_engine_item_abi_verified=true;
    // 82642C08 reapplies this item's descriptor immediately before
    // the real mesh draw. Keep that exact live binding, not an earlier
    // virtual callback's material. Actual XDK slots are rechecked at Draw.
    if(owner->material && prior_applied_material_.descriptor==owner->material)
      g_applied_item_material=prior_applied_material_;
    return;
  }
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
  if(!touched_)return;
  // Nested item invocations are permitted, but only restore into the
  // EXACT original source shader frame. Never revive stale pointers
  // after a different shader nested on top or capture was disabled.
  if(!shader_source_depth_ ||
     shader_source_depth_!=g_source_shader_method_depth) {
    g_applied_item_material={};
    return;
  }
  auto& parent=g_source_shader_method_stack[shader_source_depth_-1u];
  if(parent.original_shader_instance_guest!=owning_shader_instance_) {
    g_applied_item_material={};
    return; // the original owning shader changed; never revive stale material
  }
  // The outer item may resume after a nested render-item call. Retain its
  // source-authenticated descriptor; ReadUsaItemPixelTextures validates
  // both the actual descriptor and CURRENT hardware slot objects at Draw.
  g_applied_item_material=prior_applied_material_;
  parent.original_engine_item_method_guest=previous_method_;
  parent.original_engine_render_item_guest=previous_item_;
  parent.original_engine_render_item_context_guest=previous_context_;
  parent.original_engine_render_item_vtable_guest=previous_item_vtable_;
  parent.original_engine_render_item_material_guest=previous_material_;
  parent.original_engine_item_material_source_verified=
      previous_material_verified_;
  parent.original_engine_item_abi_verified=previous_verified_;
}

// Original USA XDK 6534 index buffer: GPU address at +24,
// allocation byte length at +28 (both PPC big endian). This capture NEVER
// trusts the address without the checked ReadGuest, nor logs game bytes.
std::shared_ptr<const OriginalGuestBufferBytes> SourceIndexBuffer(
    uint32_t object,void* guest_base,std::optional<UsaIndexBufferBinding>& binding) {
  binding=ReadUsaIndexBufferBinding(object,ReadGuest,guest_base);
  if(!binding)return {};
  return g_original_buffers.Capture(object,binding->address,binding->bytes,ReadGuest,guest_base);
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
    preview.reset(); // source snapshot no longer authorizes any GPU work
    std::lock_guard lock(g_mutex);
    if(g_frames_for_native.DiscardIfSequence(sequence))
      g_verified_worker_frame.reset(); // never reuse its stale owner
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
    // Keep the FIRST actual replay failure visible in NativeX's existing
    // log. Previously the original Draw/stage diagnostic was discarded
    // here, leaving only a generic "no Vulkan image" message.
    if(delivered->frame.sequence<=5 || delivered->frame.sequence%120==0) {
      try { throw; }
      catch(const std::exception& error) {
        REXLOG_WARN("[HULK/EOR-REPLAY] frame={} rejected native replay: {}; "
                    "no verified Vulkan image claimed",
                    delivered->frame.sequence,error.what());
      } catch (...) {
        REXLOG_WARN("[HULK/EOR-REPLAY] frame={} rejected native replay "
                    "(unknown exception); no verified Vulkan image claimed",
                    delivered->frame.sequence);
      }
    }
    return false;
  }
}
// Caller holds g_mutex, so the recorded header belongs to this exact
// Present and Draw ordinal in either the full or identity-only path.
static void ObserveBoundShaderIdentity(RawDraw& draw,uint32_t vertex,
                                      uint32_t pixel,uint8_t* guest_base) {
  if (!g_original_shader_capture_index ||
      g_collector.NextDrawOrdinal() >= FrameCapture::kMaxDraws)return;
  const auto& sources = *g_original_shader_capture_index;
  const uint64_t frame = g_collector.NextFrameSequence();
  const uint32_t ordinal = g_collector.NextDrawOrdinal();
  // Keep independently matched ambiguous candidates from the SAME read.
  // Retrying guest object reads for each pairing can observe a different
  // allocation, and repeats the expensive header and physical hashes.
  auto v=aot::ObserveOriginalBoundXdkShader(sources,vertex,aot::Stage::Vertex,
      ReadGuest,guest_base,frame,ordinal,nullptr,true);
  auto p=aot::ObserveOriginalBoundXdkShader(sources,pixel,aot::Stage::Pixel,
      ReadGuest,guest_base,frame,ordinal,nullptr,true);
  auto vs=v ? aot::ResolveOriginalLiveXdkShaderSource(sources,*v,vertex,
      aot::Stage::Vertex,frame,ordinal) : std::nullopt;
  auto ps=p ? aot::ResolveOriginalLiveXdkShaderSource(sources,*p,pixel,
      aot::Stage::Pixel,frame,ordinal) : std::nullopt;
  if(v && p && g_original_effect_capture_catalog) {
    const auto& effects=*g_original_effect_capture_catalog;
    if(vs && !ps && !p->alias_live_physical.empty()) {
      p->alias_paired_pixel_source=effects.UniquePixelForVertex(*vs);
      if(p->alias_paired_pixel_source)
        ps=aot::ResolveOriginalLiveXdkShaderSource(sources,*p,pixel,
            aot::Stage::Pixel,frame,ordinal);
    } else if(ps && !vs && !v->alias_live_physical.empty()) {
      v->alias_paired_vertex_source=effects.UniqueVertexForPixel(*ps);
      if(v->alias_paired_vertex_source)
        vs=aot::ResolveOriginalLiveXdkShaderSource(sources,*v,vertex,
            aot::Stage::Vertex,frame,ordinal);
    } else if(!vs && !ps && aot::SelectJointOriginalLiveAliasPair(
        sources,effects,*v,*p,vertex,pixel,frame,ordinal)) {
      vs=aot::ResolveOriginalLiveXdkShaderSource(sources,*v,vertex,
          aot::Stage::Vertex,frame,ordinal);
      ps=aot::ResolveOriginalLiveXdkShaderSource(sources,*p,pixel,
          aot::Stage::Pixel,frame,ordinal);
    }
    if(vs && ps)
      draw.observed_live_eor_effect_pass=aot::ResolveOriginalLiveEorEffectPass(
          sources,effects,*v,*p,vertex,pixel,frame,ordinal);
    // A paired alias is valid only with its independently authenticated
    // partner and exact original effect. Never publish an unresolved alias.
    if(!draw.observed_live_eor_effect_pass) {
      if(v->alias_paired_vertex_source)vs.reset();
      if(p->alias_paired_pixel_source)ps.reset();
    }
  }
  if(vs)draw.observed_live_vertex_shader_header=std::move(v);
  if(ps)draw.observed_live_pixel_shader_header=std::move(p);
}
void CapturePpcDraw(Kind kind, uint32_t guest, const PPCContext& ctx,
                    uint8_t* guest_base) {
  if (!CaptureEnabled()) return;
  RawDraw draw{
      kind, guest, static_cast<uint32_t>(ctx.lr),
      {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,
       ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32}};
  // Direct rendering takes priority over the separate research-only full
  // buffer dump. A persisted diagnostic checkbox must not force megabytes
  // of copies for each Draw in a frame already rejected by the shader gate.
  const bool replay=REXCVAR_GET(hulk_eor_vulkan_replay);
  const bool diagnostic_mesh=!replay &&
      REXCVAR_GET(hulk_eor_xdk_source_mesh_capture);
  // A failed Present must not starve the genuine texture owner cache.
  // Original EoR material scopes can establish immutable archive ownership
  // even if another Draw made this frame ineligible for presentation.
  // One unknown VS/PS/effect makes a Present ineligible, but must not
  // suppress source authentication of later original Draws. Hulk can issue
  // thousands of world/UI draws after an early unsupported source shader:
  // skipping them hid their genuine XDK objects, engine-method ownership,
  // material texture keys and geometry from the source-to-native bridge.
  // Continue the normal lightweight per-Draw provenance capture so the
  // complete source sequence can be accounted for. The expensive shader
  // constant and mesh snapshots remain gated by g_frame_shader_complete
  // below, and a failed shader still prohibits native GPU submission.
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
  std::optional<OriginalShaderObjects> identity_only;
  if(kind==Kind::DrawIndexedVertices || kind==Kind::DrawVertices || kind==Kind::EndVertices) {
    // The owner log shows hundreds of vertex-only draws in rejected
    // city frames. Preserve their real VS identity and ordinal, but do
    // not collect unused surfaces/matrices without a live pixel object.
    // Full research capture and engine material scopes retain all data.
    if(replay && !g_frame_shader_complete.load(std::memory_order_relaxed) &&
       !draw.original_engine_shader_method &&
       !REXCVAR_GET(hulk_eor_xdk_source_mesh_capture)) {
      const auto objects=CaptureUsaShaderObjects(ctx.r3.u32,ReadGuest,guest_base);
      if(objects && objects->vertex && !objects->pixel)identity_only=objects;
    }
    if(!identity_only)
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
  if (draw.original_engine_shader_method && draw.observed_vs_matrices) {
    const auto& owner=*draw.original_engine_shader_method;
    if (owner.original_decal_item_material_binding_verified &&
        ValidOriginalDecalItemMaterialBinding(draw)) {
      draw.original_pixel_texture_keys=ReadUsaMaterialPixelTextures(
          owner.original_decal_material_context_guest,owner.original_decal_material_word0,
          owner.original_decal_material_word8,draw.observed_vs_matrices->original_texture_guest_objects,
          ReadGuest,guest_base,&draw.original_pixel_texture_owners);
    } else if (owner.original_engine_item_abi_verified &&
               owner.original_engine_item_material_source_verified &&
               owner.original_pass_vs_ps_xdk_binding_verified) {
      draw.original_pixel_texture_keys=ReadUsaItemPixelTextures(
          g_applied_item_material,owner.original_pass_material_context_guest,
          owner.original_engine_render_item_material_guest,
          draw.observed_vs_matrices->original_texture_guest_objects,ReadGuest,guest_base,
          &draw.original_pixel_texture_owners);
    } else if (owner.original_pass_vs_ps_xdk_binding_verified &&
               owner.original_pass_sampler_slots_verified &&
               g_applied_item_material.context &&
               g_applied_item_material.context==
                   owner.original_pass_material_context_guest &&
               g_applied_item_material.descriptor &&
               g_applied_item_material.selector &&
               g_applied_item_material.parameters &&
               g_applied_item_material==ReadUsaAppliedMaterial(
                   owner.original_pass_material_context_guest,
                   g_applied_item_material.descriptor,ReadGuest,guest_base)) {
      // EFontShader and other non-Decal EoR classes can bind an original
      // material without the independent per-item descriptor ABI. The
      // setter is observed in THIS original shader-method TLS scope;
      // compare the descriptor's CURRENT game bytes, pass material context
      // and bound XDK texture-object pointers before claiming a texture
      // archive hash. Never infer an atlas from a similar shader or logo.
      const auto current=ReadUsaMaterialPixelTextures(
          owner.original_pass_material_context_guest,
          g_applied_item_material.selector,g_applied_item_material.parameters,
          draw.observed_vs_matrices->original_texture_guest_objects,
          ReadGuest,guest_base,&draw.original_pixel_texture_owners);
      for(uint32_t slot=0;slot<current.size();++slot) {
        if(owner.original_pass_sampler_slot_mask & (uint16_t(1u)<<slot))
          draw.original_pixel_texture_keys[slot]=current[slot];
        else draw.original_pixel_texture_owners[slot]={};
      }
    }
  }
  // Two dominant USA callsites have a source-defined geometry owner even
  // when a virtual material method was not active. Validate the original
  // caller AND the wrapper's tail branch once per immutable guest image.
  if(draw.observed_vs_matrices &&
     (draw.lr==0x826676A8u || draw.lr==0x8268BFE4u)) {
    struct SourceImage {uint8_t* base=nullptr;bool indexed=false,quad=false;};
    static thread_local SourceImage image;
    if(image.base!=guest_base) {
      image={guest_base,false,false};
      const auto fingerprint=[&](uint32_t addr,size_t size,uint64_t expected) {
        std::array<std::byte,120> bytes{};
        if(size>bytes.size() || !ReadGuest(guest_base,addr,bytes.data(),size))return false;
        uint64_t hash=0xcbf29ce484222325ull;
        for(size_t i=0;i<size;++i)
          hash=(hash^std::to_integer<uint8_t>(bytes[i]))*0x100000001b3ull;
        return hash==expected;
      };
      image.indexed=fingerprint(0x82667648u,0x78u,0x3dd4ad253a230bdfull) &&
                    fingerprint(0x82676288u,0x34u,0x24259fb7d422584dull);
      image.quad=fingerprint(0x8268bf98u,0x64u,0x44d4d951bb925d7full) &&
                 fingerprint(0x826762c0u,0x28u,0x03f51a528c7ae7400ull);
    }
    if((draw.lr==0x826676A8u && image.indexed) ||
       (draw.lr==0x8268BFE4u && image.quad)) {
      draw.original_geometry_wrapper=ReadOriginalGeometryWrapperOwner(
          draw,ctx.r30.u32,ctx.r31.u32,ReadGuest,guest_base);
      // An incomplete optional parent must not poison a real Draw that
      // has independently verified geometry. Remove ONLY the failed
      // attribution; do not remove the Draw or promote any shader or
      // texture. The direct runtime path still requires exact resources.
      if(draw.original_geometry_wrapper && !OriginalGeometryWrapperHasValidParent(draw))
        draw.original_engine_shader_method.reset();
    }
  }
  // Keep buffer revisions and their Draw in the same Present epoch.
  std::lock_guard lock(g_mutex);
  // Learn authentic material texture ownership before shader admission.
  // An otherwise incomplete frame still contains useful exact ERTexture
  // bindings from the Draws whose material owner was actually observed.
  for(const auto& witness:draw.original_pixel_texture_owners)
    if(witness.asset)g_original_texture_owners.Remember(witness,ReadGuest,guest_base);
  // Read both objects from this Draw's device shadow. UI, intro and world
  // Draws can bind shaders outside an observed engine shader-method scope.
  // Every source Draw must authenticate its own bound VS/PS and EoR
  // effect, even after an earlier Draw made this Present ineligible.
  // The old g_frame_shader_complete shortcut silently discarded ALL later
  // shader identities when the 5194-program AOT bank was complete, so the
  // bridge could never establish full coverage or identify valid later
  // materials. Only expensive constant/mesh snapshots are short-circuited.
  if(draw.observed_vs_matrices &&
     draw.observed_vs_matrices->original_guest_device==draw.gpr[0])
    ObserveBoundShaderIdentity(draw,draw.observed_vs_matrices->original_vs_guest_object,
                               draw.observed_vs_matrices->original_ps_guest_object,guest_base);
  else if(identity_only)
    ObserveBoundShaderIdentity(draw,identity_only->vertex,identity_only->pixel,guest_base);
  // Depth-only draws already read the fixed state to classify the absent
  // PS. Keep that exact same Draw snapshot for native pipeline creation.
  std::optional<OriginalXdkFixedGraphicsState> stage_fixed;
  if(draw.observed_vs_matrices) {
    const auto& live=*draw.observed_vs_matrices;
    draw.original_pixel_stage=CaptureUsaPixelStageKind(live.original_guest_device,
        {live.original_vs_guest_object,live.original_ps_guest_object},ReadGuest,guest_base,
        replay?&stage_fixed:nullptr);
  } else if(identity_only)
    draw.original_pixel_stage=CaptureUsaPixelStageKind(draw.gpr[0],*identity_only,ReadGuest,guest_base);
  if(replay && !HasOriginalDrawProgram(draw)) {
    // Preserve the very first failed original program pairing in a source
    // Present. A later incomplete-frame shortcut must never hide WHICH
    // genuine XDK Draw first blocked the independent EoR replay.
    const bool first=g_frame_shader_complete.exchange(
        false,std::memory_order_relaxed);
    if(first) {
      const auto frame=g_collector.NextFrameSequence();
      if(frame<=5 || frame%120u==0u)
        REXLOG_WARN("[HULK/EOR-FIRST-SHADER-GAP] frame={} draw={} "
                    "XDK=0x{:08X} LR=0x{:08X} vs_object=0x{:08X} "
                    "ps_object=0x{:08X} source_vs={} source_ps={} effect_pass={}",
                    frame,g_collector.NextDrawOrdinal(),draw.function,draw.lr,
                    draw.observed_vs_matrices ?
                        draw.observed_vs_matrices->original_vs_guest_object : 0u,
                    draw.observed_vs_matrices ?
                        draw.observed_vs_matrices->original_ps_guest_object : 0u,
                    bool(draw.observed_live_vertex_shader_header),
                    bool(draw.observed_live_pixel_shader_header),
                    bool(draw.observed_live_eor_effect_pass));
    }
  }
  // Keep observing later genuine shader/material identities after the first
  // gap, but do not repeatedly copy full guest registers and mesh allocations
  // for an already unpresentable frame. Explicit mesh research can still
  // request those copies, independent of normal gameplay.
  // The final timeline gate rejects invalid engine parents and unsupported
  // Draw ABIs even with complete VS/PS coverage. Apply the SAME gate before
  // copying megabytes of live guest memory for a frame that cannot replay.
  // EndVertices requires its actual inline glyph payload, checked below.
  if(replay && (!OriginalGeometryWrapperHasValidParent(draw) ||
      (kind==Kind::DrawIndexedVertices && !DecodeOriginalIndexedXdkCall(draw)) ||
      (kind==Kind::DrawVertices && !DecodeOriginalVerticesXdkCall(draw)) ||
      kind==Kind::DrawVerticesUP))
    g_frame_shader_complete.store(false,std::memory_order_relaxed);
  const bool verified_draw_program=draw.observed_vs_matrices && HasOriginalDrawProgram(draw);
  const bool capture_heavy=diagnostic_mesh ||
      (replay && verified_draw_program &&
       g_frame_shader_complete.load(std::memory_order_relaxed));
  if(replay && !capture_heavy)++g_frame_heavy_capture_skipped;
  if(capture_heavy && draw.observed_vs_matrices) {
      // Store the full register bank only for a viable replay frame or
      // explicit mesh research. The lightweight identity observer does
      // not need 256 original vertex AND pixel float4 registers:
      // later source-proven native SPIR-V material declarations can read
      // exact per-Draw shader parameters without another guest memory read.
      try {
        auto constants=std::make_shared<OriginalShaderRegisters>();
        if(CaptureUsaAllShaderRegisters(ctx.r3.u32,ReadGuest,
                                        guest_base,*constants)) {
          if(REXCVAR_GET(hulk_eor_vulkan_replay)) {
            CaptureUsaPixelFetches(ctx.r3.u32,ReadGuest,guest_base,*constants);
            CaptureUsaVertexFetches(ctx.r3.u32,ReadGuest,guest_base,*constants);
            CaptureUsaBooleanRegisters(ctx.r3.u32,ReadGuest,guest_base,*constants);
            constants->fixed_graphics_state=stage_fixed?std::move(stage_fixed):
                CaptureUsaFixedGraphicsState(ctx.r3.u32,ReadGuest,guest_base);
          }
          draw.observed_shader_constants=std::move(constants);
        }
      } catch (const std::bad_alloc&) {
        // Never crash game logic on a diagnostic snapshot allocation.
      }
  }
  // A failed required register read cannot be repaired on the Vulkan worker.
  // Stop expensive texture/mesh acquisition for this epoch, not the live
  // shader/effect identity observer or an explicitly requested diagnostic.
  if(replay && capture_heavy && !HasRequiredReplayRegisters(draw))
    g_frame_shader_complete.store(false,std::memory_order_relaxed);
  const bool replay_geometry=replay && capture_heavy && verified_draw_program &&
      g_frame_shader_complete.load(std::memory_order_relaxed);
  if(replay_geometry && g_original_shader_capture_index) {
    const auto& sources=*g_original_shader_capture_index;
    if(auto& vertex=draw.observed_live_vertex_shader_header;
       vertex && draw.observed_shader_constants && draw.observed_shader_constants->valid_vertex_fetch) {
      try {
        if(const auto sha=aot::ResolveOriginalLiveXdkShaderSource(
              sources,*vertex,draw.observed_vs_matrices->original_vs_guest_object,
              aot::Stage::Vertex,g_collector.NextFrameSequence(),g_collector.NextDrawOrdinal())) {
          vertex->vertex_sampling=aot::CaptureOriginalVertexSamplerControls(
              *vertex,*sha,ReadGuest,guest_base,g_vertex_sampling,&sources);
          if(vertex->vertex_sampling)
            for(uint32_t slot=0;slot<4u;++slot) {
              if(!(vertex->vertex_sampling->sampled_slots&(1u<<slot)))continue;
              const auto bound=draw.observed_vs_matrices->original_vertex_texture_guest_objects[slot];
              if(const auto owner=g_original_texture_owners.Find(bound,ReadGuest,guest_base))
                draw.original_vertex_texture_owners[slot]=*owner;
            }
        }
      } catch(const std::bad_alloc&) { /* No source policy means no verified VS texture draw. */ }
    }
    if(auto& pixel=draw.observed_live_pixel_shader_header;
       pixel && REXCVAR_GET(hulk_eor_vulkan_replay) && draw.observed_shader_constants) {
      try {
        if(const auto sha=aot::ResolveOriginalLiveXdkShaderSource(
              sources,*pixel,draw.observed_vs_matrices->original_ps_guest_object,
              aot::Stage::Pixel,g_collector.NextFrameSequence(),g_collector.NextDrawOrdinal())) {
          pixel->pixel_sampling=aot::CaptureOriginalPixelSamplerControls(
              *pixel,*sha,ReadGuest,guest_base,g_pixel_sampling,&sources);
          if(pixel->pixel_sampling) {
            const auto& live=*draw.observed_vs_matrices;
            for(uint32_t slot=0;slot<16u;++slot) {
              if(!(pixel->pixel_sampling->sampled_slots&(1u<<slot)))continue;
              auto& witness=draw.original_pixel_texture_owners[slot];
              if(!witness.asset && !draw.original_pixel_texture_keys[slot])
                if(const auto owner=g_original_texture_owners.Find(
                     live.original_texture_guest_objects[slot],ReadGuest,guest_base)) {
                  witness=*owner;
                  draw.original_pixel_texture_keys[slot]=owner->hash;
                }
            }
          }
        }
      } catch(const std::bad_alloc&) { /* Missing source policy rejects only direct replay. */ }
    }
  }
  // Recovering an authentic cached ERTexture can establish the atlas
  // only in the sampler loop above. Capture its live s0 descriptor now,
  // not before ownership recovery, or StageVerifiedOriginalFontDraw
  // rejects an otherwise complete original menu/HUD frame.
  if(replay_geometry)
    draw.original_font_fetch=CaptureUsaFontFetchAtDraw(draw,ReadGuest,guest_base);
  // Only a requested direct Vulkan render performs byte-exact guest
  // resource capture; diagnostics-only XDK observer is unaffected.
  if((diagnostic_mesh || replay_geometry) && draw.observed_vs_matrices) {
    try {
      const auto& state=*draw.observed_vs_matrices;
      // Source snapshots are deduplicated by the existing per-Present
      // full-byte checked cache. Repeated Xbox Draws retain identical GPU
      // buffer owners; changed vertex skinning creates a new generation.
      // The per-Present immutable allocation budget is shared by all 16
      // streams. Prioritize the mandatory vertex/index pair BEFORE optional
      // stale bound slots: otherwise an unused slot 15 can exhaust the budget
      // and turn a valid real DrawIndexed into a missing source IB.
      if(kind==Kind::EndVertices) {
        draw.inline_glyphs=CaptureUsaInlineGlyphs(
            draw,ctx.r31.u32,ctx.r1.u32,ReadGuest,guest_base,g_original_glyph_buffers);
      } else {
        draw.captured_vertex_bindings=true;
        draw.vertex_declaration=ReadUsaVertexDeclaration(
            state.original_vertex_declaration_guest_object,ReadGuest,guest_base);
        const auto capture_stream=[&](uint32_t slot) {
          const uint32_t object=state.original_vertex_buffer_guest_objects[slot];
          auto& binding=draw.vertex_bindings[slot];
          binding=ReadUsaVertexStreamBinding(state.original_guest_device,slot,object,ReadGuest,guest_base);
          if(binding)draw.source_vertex_streams[slot]=g_original_buffers.Capture(object,
              binding->allocation_address,binding->allocation_bytes,ReadGuest,guest_base);
        };
        // SetStreamSource retains old bindings. Only the current declaration
        // can consume them. Preserve legacy capture if the declaration is unreadable.
        const uint16_t streams=draw.vertex_declaration ? draw.vertex_declaration->streams : UINT16_MAX;
        if(streams&1u)capture_stream(0);
        draw.source_vb0=draw.source_vertex_streams[0];
        if(kind==Kind::DrawIndexedVertices)
          draw.source_ib=SourceIndexBuffer(state.original_index_buffer_guest_object,guest_base,draw.index_binding);
        for(uint32_t slot=1;slot<draw.source_vertex_streams.size();++slot)
          if((streams&(1u<<slot)) && state.original_vertex_buffer_guest_objects[slot])capture_stream(slot);
      }
    } catch (const std::bad_alloc&) {
      draw.source_vb0.reset();
      draw.source_ib.reset();
      draw.source_vertex_streams={};
    }
  }
  if(replay && capture_heavy && !HasRequiredReplaySnapshot(draw))
    g_frame_shader_complete.store(false,std::memory_order_relaxed);
  g_collector.Record(std::move(draw));
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
  if(kind==CommandKind::ClearF) {
    event.captured_clear=CaptureOriginalXdkClearF(
        event.gpr,ctx.f1.f64,ReadGuest,guest_base);
    if(event.captured_clear && REXCVAR_GET(hulk_eor_vulkan_replay) &&
       g_frame_shader_complete.load(std::memory_order_relaxed)) {
      auto targets=CaptureUsaVsMatrices(event.gpr[0],ReadGuest,guest_base);
      auto fixed=CaptureUsaFixedGraphicsState(event.gpr[0],ReadGuest,guest_base);
      if(targets && fixed) {
        event.captured_clear_targets=std::make_shared<const OriginalVsMatrices>(std::move(*targets));
        event.captured_clear_fixed=std::make_shared<const OriginalXdkFixedGraphicsState>(std::move(*fixed));
      }
    }
  }
  if(kind==CommandKind::Resolve) {
    event.captured_resolve=CaptureOriginalColorResolveRegion(event.gpr,ReadGuest,guest_base);
    if(event.captured_resolve && (event.gpr[2] || event.gpr[4])) {
      if(const auto state=CaptureUsaVsMatrices(event.gpr[0],ReadGuest,guest_base)) {
        event.captured_resolve_raster=state->raster;
        event.captured_resolve_color_owner=state->original_rt0_guest_object;
      }
    }
  }
  std::lock_guard lock(g_mutex);
  if(REXCVAR_GET(hulk_eor_vulkan_replay) && kind==CommandKind::ClearF &&
     (!event.captured_clear || event.captured_clear->flags!=event.gpr[1]))
    g_frame_shader_complete.store(false,std::memory_order_relaxed);
  g_collector.RecordCommand(event);
}
void EndPpcHook(native_x_renderer::Call kind) {
  if (kind != native_x_renderer::Call::kPresent || !CaptureEnabled()) return;
  Frame frame;
  std::vector<CallsiteEvidenceRecord> hot;
  uint64_t untracked=0;
  uint32_t heavy_skipped=0;
  {
    std::lock_guard lock(g_mutex);
    frame = g_collector.Finish();
    heavy_skipped=g_frame_heavy_capture_skipped;
    g_frame_heavy_capture_skipped=0;
    g_frame_shader_complete=true;
    g_original_buffers.Reset();
    g_original_glyph_buffers.Reset();
    g_callsite_evidence.Observe(frame);
    // This independent original PPC telemetry also works with the
    // compatibility Xenos renderer and NO compiled SPIR-V shader bank.
    // Compute once per real Present, not on every ImGui refresh. A late
    // UI query can never see half-updated VS/PS/effect counts.
    OriginalEorSourceCoverage coverage{};
    coverage.original_frame_sequence=frame.sequence;
    coverage.original_xdk_draws=static_cast<uint32_t>(frame.calls.size());
    coverage.complete_xdk_timeline=
        !frame.dropped && !frame.dropped_commands &&
        ValidateOriginalCommandTimeline(frame);
    for(const auto& draw:frame.calls) {
      if(draw.source_vb0 && !draw.source_vb0->bytes.empty())
        ++coverage.original_live_vb0;
      if(draw.source_ib && !draw.source_ib->bytes.empty())
        ++coverage.original_live_ib;
      if(draw.kind==Kind::DrawIndexedVertices &&
         draw.source_vb0 && draw.source_ib &&
         !draw.source_vb0->bytes.empty() &&
         !draw.source_ib->bytes.empty())
        ++coverage.original_indexed_with_exact_buffer_bytes;
      if(draw.observed_live_vertex_shader_header)
        ++coverage.original_vs_headers;
      if(draw.observed_live_pixel_shader_header)
        ++coverage.original_ps_headers;
      if(draw.observed_live_vertex_shader_header &&
         draw.observed_live_pixel_shader_header)
        ++coverage.original_vs_ps_pairs;
      if(draw.observed_live_eor_effect_pass)
        ++coverage.original_effect_passes;
    }
    g_original_source_coverage=coverage;
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
    uint32_t original_header_vs=0,original_header_ps=0;
    uint32_t original_header_pairs=0,original_effect_passes=0;
    uint32_t vertex_only_depth=0,vertex_only_no_color=0,unresolved_stage=0;
    for (const RawDraw& call : frame.calls) {
      if(call.observed_live_eor_effect_pass)++original_effect_passes;
      else if(call.observed_live_vertex_shader_header &&
              call.original_pixel_stage==OriginalPixelStageKind::VertexOnlyDepthStencil)
        ++vertex_only_depth;
      else if(call.observed_live_vertex_shader_header &&
              call.original_pixel_stage==OriginalPixelStageKind::VertexOnlyNoColor)
        ++vertex_only_no_color;
      else ++unresolved_stage;
      if(call.observed_live_vertex_shader_header)++original_header_vs;
      if(call.observed_live_pixel_shader_header)++original_header_ps;
      if(call.observed_live_vertex_shader_header &&
         call.observed_live_pixel_shader_header)++original_header_pairs;
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
      case Kind::EndVertices:
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
    REXLOG_INFO("[HULK/EOR-SOURCE-SHA] original XDK Draw frame={} "
                "virtual_header_vs_verified={} virtual_header_ps_verified={} "
                "live_vs_ps_pairs_verified={} "
                "original_effect_passes_verified={} total_draws={}; "
                "0 means source row+4 header ABI not proven, not a "
                "license to substitute ShaderEffects archive bytes",
                frame.sequence,original_header_vs,original_header_ps,
                original_header_pairs,original_effect_passes,
                frame.calls.size());
    REXLOG_INFO("[HULK/EOR-STAGE-COVERAGE] frame={} effect_pairs={} "
                "vertex_only_depth_stencil={} vertex_only_no_color={} unresolved={} "
                "vertex-only stages classified separately from effect pairs",
                frame.sequence,original_effect_passes,vertex_only_depth,vertex_only_no_color,unresolved_stage);
    REXLOG_INFO("[HULK/EOR-CAPTURE-COST] frame={} full_snapshot_skipped={} (missing pair or already rejected frame)",frame.sequence,heavy_skipped);
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
#ifdef _WIN32
    if(g_notify_exclusive_frame)g_notify_exclusive_frame();
#endif
  }
}
bool InstallOriginalShaderCaptureIndex(
    std::shared_ptr<const aot::OriginalVirtualShaderIdentity> original,
    std::shared_ptr<const aot::EffectCatalog> original_effects) noexcept {
  // This project deliberately pins the USA ShaderEffects source census:
  // a partial/truncated private identity index must not label guest memory.
  if(!original || original->size()!=5194u ||
     (original_effects &&
      (original_effects->PairCount()!=2597u ||
       original_effects->ShaderCount()!=original->size())))
    return false;
  std::lock_guard lock(g_mutex);
  if(g_offline_shader_pack &&
     ((g_offline_shader_pack->virtual_ids &&
       g_offline_shader_pack->virtual_ids!=original &&
       !g_offline_shader_pack->virtual_ids->SameOriginalSources(*original)) ||
      (original_effects &&
       g_offline_shader_pack->effects!=original_effects &&
       !g_offline_shader_pack->effects->SameOriginalSources(*original_effects))))
    return false;
  g_original_shader_capture_index=std::move(original);
  g_original_effect_capture_catalog=std::move(original_effects);
  return true;
}
void ClearOriginalShaderCaptureIndex() noexcept {
  std::lock_guard lock(g_mutex);
  g_original_effect_capture_catalog.reset();
  g_pixel_sampling.clear();
  g_vertex_sampling.clear();
  g_original_shader_capture_index.reset();
  g_worker_catalog_factory_last_attempt=0;
  // Any old catalogue attached to that original shader generation must
  // not be allowed to authorize a new independent Vulkan Present.
  g_worker_owner_catalog.reset();
  g_verified_worker_frame.reset();
}
bool InstallVerifiedSourceShaderPackFromDirectory(
    const std::filesystem::path& directory) noexcept {
  if(directory.empty())return false;
  try {
    auto shaders=std::make_shared<aot::ShaderBank>();
    auto effects=std::make_shared<aot::EffectCatalog>();
    auto reflection=std::make_shared<aot::ReflectionBank>();
    auto samplers=std::make_shared<aot::OriginalXdkShaderSamplers>();
    auto virtual_ids=std::make_shared<aot::OriginalVirtualShaderIdentity>();
    // All local paths are under ONE caller-selected private root; loading
    // metadata without the compiled original code must never announce a
    // source-complete native Vulkan renderer.
    // The private Hulk XenosRecomp/DXC compiler emits
    // hulk_shader_bank.bin (with HEORSPV1 magic). Accept its generated
    // filename as well as the explicit HEORSPV1.bin install alias.
    const auto alias=directory/"HEORSPV1.bin";
    const auto generated=directory/"hulk_shader_bank.bin";
    // Prefer the compiler's atomically replaced cumulative bank. The
    // explicit HEORSPV1 alias can still exist from an OLDER partial run;
    // loading it first permanently masked newly compiled original VS/PS.
    shaders->Load(std::filesystem::is_regular_file(generated) ?
                  generated : alias);
    // NativePC's original guest loader already accepts the historical
    // private hulk_* filenames and source HEOR* aliases. The Vulkan
    // worker must honor the SAME private generation, or the game
    // authenticates original VS/PS at the PPC hook while the GPU side
    // silently rejects their matching source material catalog.
    const auto source_file=[&](const char* generated_name,
                               const char* source_alias) {
      const auto preferred=directory/generated_name;
      return std::filesystem::is_regular_file(preferred) ?
          preferred : directory/source_alias;
    };
    effects->Load(source_file("hulk_effect_ledger.bin","HEORMAT2.bin"));
    reflection->Load(source_file("hulk_shader_reflection.bin","HEORREF1.bin"));
    samplers->Load(source_file("hulk_sampler_bank.bin","HEORSMP1.bin"));
    virtual_ids->Load(source_file(
        "hulk_shader_virtual_fingerprints.bin","HEORVRT1.bin"));
    std::filesystem::path game_root;
    {
      std::lock_guard lock(g_mutex);
      game_root=g_original_game_asset_root;
    }
    (void)virtual_ids->LoadOriginalPixelDebugAliases(game_root);
    // The source-identity ledgers must cover the complete original USA
    // ShaderEffects archive, but HEORSPV1 may contain only the original
    // VS/PS programs actually compiled so far. Each real frame still
    // requires EVERY current Draw shader to be present and checked by its
    // original SHA: no generic fallback shader or missing Draw can pass.
    // Requiring all 5,194 shader variants just to start one valid menu
    // scene made incremental AOT impossible without improving correctness.
    if(!shaders->size() || !shaders->expected() ||
       shaders->size()>shaders->expected() ||
       shaders->expected()!=effects->ShaderCount() ||
       shaders->expected()!=reflection->size() ||
       shaders->expected()!=samplers->ShaderCount() ||
       shaders->expected()!=virtual_ids->size())
      {
        REXLOG_WARN("[HULK/EOR-SOURCE-PACK] incompatible counts compiled={} expected={} effects={} pairs={} reflection={} samplers={} virtual={}",
                    shaders->size(),shaders->expected(),effects->EffectCount(),effects->PairCount(),reflection->size(),samplers->ShaderCount(),virtual_ids->size());
        return false;
      }
    const bool installed=InstallVerifiedSourceShaderPack(
        std::move(shaders),std::move(effects),std::move(reflection),
        std::move(samplers),std::move(virtual_ids));
    if(!installed)REXLOG_WARN("[HULK/EOR-SOURCE-PACK] live source generation mismatch");
    return installed;
  } catch (const std::exception& e) {
    REXLOG_WARN("[HULK/EOR-SOURCE-PACK] loader rejected source bank: {}",e.what());
    return false;
  } catch (...) {
    // Missing files, incomplete AOT compiler outputs, corrupt SPIR-V or
    // archive digest mismatches NEVER replace the current verified bank.
    return false;
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
  // The app's read-only observer and the render worker may load separate
  // shared_ptr instances of the exact SAME privately SHA-authenticated
  // HEORVRT1 and HEORMAT2 files. Address inequality alone used to reject
  // every Vulkan source-pack handoff even when all 5194 source programs
  // and every original effect/pass matched. Compare immutable content;
  // preserve rejection for any real differing source generation.
  if(virtual_ids && g_original_shader_capture_index &&
     virtual_ids!=g_original_shader_capture_index &&
     !virtual_ids->SameOriginalSources(*g_original_shader_capture_index))
    return false;
  if(g_original_effect_capture_catalog &&
     effects!=g_original_effect_capture_catalog &&
     !effects->SameOriginalSources(*g_original_effect_capture_catalog))
    return false;
  // Retain the authenticated archive alias index when a cumulative
  // shader-bank refresh loaded the same HEORVRT1 but no archive path.
  if(virtual_ids && g_original_shader_capture_index &&
     g_original_shader_capture_index->PixelDebugAliasCount()>virtual_ids->PixelDebugAliasCount())
    virtual_ids=g_original_shader_capture_index;
  // A new ShaderEffects generation invalidates source-bound material PSOs,
  // live shader identities and every reusable source owner catalogue.
  // Register the new owner catalogue after installing the new shader pack.
  if(!g_offline_shader_pack ||
     g_offline_shader_pack->shaders!=shaders ||
     g_offline_shader_pack->effects!=effects ||
     g_offline_shader_pack->reflection!=reflection ||
     g_offline_shader_pack->samplers!=samplers ||
     g_offline_shader_pack->virtual_ids!=virtual_ids) {
    g_worker_owner_catalog.reset();
    g_worker_catalog_factory_last_attempt=0;
  }
  g_verified_worker_frame.reset();
  g_offline_shader_pack=OfflineShaderPack{
      std::move(shaders),std::move(effects),std::move(reflection),
      std::move(samplers),std::move(virtual_ids)};
  // The guest PPC Draw observer must use the SAME original source SHA
  // and effect ledger generation as the registered Vulkan shaders.
  // Previously this required a second independent registration call;
  // leaving it out meant every live XDK shader header/pass was absent,
  // even when the complete private HEOR* pack had loaded successfully.
  g_original_shader_capture_index=g_offline_shader_pack->virtual_ids;
  g_original_effect_capture_catalog=g_offline_shader_pack->effects;
  return true;
}
void InstallOriginalEorGameAssetRoot(
    const std::filesystem::path& user_original_game_root) {
  std::lock_guard lock(g_mutex);
  // This root contains only privately owned original game resources.
  // No public repo or packaged native executable embeds game archive data.
  g_original_game_asset_root=user_original_game_root;
}
void ClearOriginalEorGameAssetRoot() noexcept {
  std::lock_guard lock(g_mutex);
  g_original_game_asset_root.clear();
  g_original_texture_owners.Reset();
  g_original_material_candidate_index.clear();
  g_original_shader_aot_demand_path.clear();
  g_original_shader_aot_requests.clear();
  g_last_incremental_spirv_file.clear();
  g_last_incremental_spirv_write={};
  g_last_incremental_spirv_size=0;
  g_last_incremental_spirv_poll=0;
}
void InstallOriginalEorMaterialCandidateIndexPath(
    const std::filesystem::path& private_source_index) {
  std::lock_guard lock(g_mutex);
  g_original_material_candidate_index=private_source_index;
  g_original_shader_aot_demand_path=private_source_index.empty() ?
      std::filesystem::path{} : private_source_index.parent_path()/"HEORREQ1.txt";
  g_original_shader_aot_requests.clear();
  g_last_incremental_spirv_file.clear();
  g_last_incremental_spirv_write={};
  g_last_incremental_spirv_size=0;
  g_last_incremental_spirv_poll=0;
}
bool HasVerifiedOriginalShaderCaptureIndex() noexcept {
  std::lock_guard lock(g_mutex);
  return g_original_shader_capture_index &&
      g_original_shader_capture_index->size()==5194u &&
      g_original_effect_capture_catalog &&
      g_original_effect_capture_catalog->PairCount()==2597u &&
      g_original_effect_capture_catalog->ShaderCount()==5194u;
}
bool HasVerifiedSourceShaderPack() noexcept {
  std::lock_guard lock(g_mutex);
  return g_offline_shader_pack && g_offline_shader_pack->shaders &&
      g_offline_shader_pack->shaders->size() &&
      g_offline_shader_pack->effects && g_offline_shader_pack->reflection &&
      g_offline_shader_pack->samplers && g_offline_shader_pack->virtual_ids;
}
void ClearVerifiedSourceShaderPack() noexcept {
  std::lock_guard lock(g_mutex);
  g_verified_worker_frame.reset();
  g_worker_owner_catalog.reset();
  g_worker_catalog_factory.reset();
  g_worker_catalog_factory_last_attempt=0;
  const auto old=g_offline_shader_pack;
  g_offline_shader_pack.reset();
  if(old && g_original_shader_capture_index==old->virtual_ids) {
    g_pixel_sampling.clear();
    g_vertex_sampling.clear();
    g_original_shader_capture_index.reset();
  }
  if(old && g_original_effect_capture_catalog==old->effects)
    g_original_effect_capture_catalog.reset();
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
  if(!g_offline_shader_pack ||
     (frame->owner_catalog &&
      frame->owner_catalog!=g_worker_owner_catalog))
    return false;
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
    // A producer may supply independently verified reusable original
    // material/mesh candidates instead of synthesizing a witness for each
    // identical XDK Draw. Acquire ONLY real already-uploaded Xbox models;
    // their host GPU buffers remain strongly owned through submission.
    std::map<uint32_t,std::shared_ptr<const model::UploadedOriginalModel>>
        candidate_models;
    std::vector<native::VerifiedXdkResourceWitness> applicable;
    applicable.reserve(frame->witnesses.size());
    for(const auto& candidate:frame->witnesses) {
      const auto& owner=std::visit([](const auto& link)
          ->const native::VerifiedDrawSource& {return link.native_draw;},
          candidate.original_source_submission);
      if(!candidate.original_guest_resource_to_material_mapping_verified ||
         !owner.engine_draw_verified || !owner.mesh_material_verified ||
         !owner.host_vertex_layout_verified || !owner.render_state_verified ||
         !owner.original_model_hash ||
         (owner.original_frame_sequence &&
          owner.original_frame_sequence!=source->sequence))
        return false;
      // Avoid loading the ENTIRE world material catalogue each Present.
      // The original XDK shader/declaration/device pair is a prefilter
      // ONLY, never a material proof. The final byte-for-byte VB/IB,
      // effect/pass, sampler, surface and per-Draw checks remain mandatory.
      const auto& expected=candidate.original_resources;
      const bool possible=std::any_of(source->calls.begin(),
          source->calls.end(),[&](const RawDraw& call) {
            if(!call.observed_vs_matrices)return false;
            const auto actual=native::ResourcesAtDraw(
                *call.observed_vs_matrices);
            if(actual.device!=expected.device ||
               actual.vertex_declaration!=expected.vertex_declaration ||
               actual.vertex_shader!=expected.vertex_shader ||
               actual.pixel_shader!=expected.pixel_shader ||
               actual.rt0!=expected.rt0 || actual.depth!=expected.depth)
              return false;
            if(call.observed_live_eor_effect_pass &&
               (call.observed_live_eor_effect_pass->effect_hash!=
                    owner.original_effect_hash ||
                call.observed_live_eor_effect_pass->pass_index!=
                    owner.original_pass_index))
              return false;
            return true;
          });
      if(!possible)continue;
      if(!candidate_models.contains(owner.original_model_hash)) {
        auto model=lookup(owner.original_model_hash);
        if(!model || model->original_resource_hash!=owner.original_model_hash ||
           model->original_identity.platform!=model::Platform::Xbox360)
          return false; // possible active source is not resident: no GPU Draw
        candidate_models.emplace(owner.original_model_hash,std::move(model));
      }
      applicable.push_back(candidate);
    }
    if(applicable.empty())return false;
    std::vector<const model::UploadedOriginalModel*> borrowed;
    borrowed.reserve(candidate_models.size());
    for(const auto& [_,model]:candidate_models)
      borrowed.push_back(model.get());
    // The actual per-Draw producer: authenticate all current PPC XDK
    // shader, buffer, sampler and material bindings against original
    // resident meshes. Reusable candidates become EXACT frame/ordinal
    // witnesses only after unique current-source selection.
    auto materialized=native::MaterializeLiveOriginalResourceWitnesses(
        *source,applicable,borrowed);
    auto source_parents=native::AttachLiveEorShaderMethodParents(
        *source,materialized);
    auto residents=native::RetainVerifiedOriginalFrameResidents(
        *source,source_parents,lookup);
    if(residents.empty())return false;
    auto complete=std::make_shared<VerifiedWorkerEorFrame>(*frame);
    complete->witnesses=std::move(source_parents);
    complete->residents=std::move(residents);
    // A newer XDK Present can replace source while retrieving models.
    // InstallVerifiedWorkerEorFrame rechecks the source sequence under
    // the original Present mutex before publishing to the GPU worker.
    return InstallVerifiedWorkerEorFrame(std::move(complete));
  } catch (...) {
    // No GPU work started. Keep the established NativeX presentation.
    return false;
  }
}
namespace {
bool InstallVerifiedWorkerEorOwnerCatalogInternal(
    std::shared_ptr<const VerifiedWorkerEorOwnerCatalog> catalog,
    const std::shared_ptr<const VerifiedWorkerEorCatalogFactory>* factory,
    const OfflineShaderPack* pack,
    const std::shared_ptr<const VerifiedWorkerEorOwnerCatalog>* expected_previous) {
  // Permit the EXISTING direct Vulkan backend to accept a purely
  // source-backed resident-model bootstrap BEFORE the first material owner
  // has been proven. It may stream real archive meshes from live XDK
  // texture hashes, but never authorizes a Draw or physical Present until
  // source witnesses and exact SPIR-V/Vulkan PSOs have been adopted.
  // Earlier the empty-catalog reject created a deadlock: no source GPU
  // mesh was eligible to upload until at least one fully verified material
  // existed, but material verification itself needed that resident mesh.
  const bool source_residency_bootstrap =
      catalog && catalog->verified_candidates.empty() &&
      catalog->verified_font_materials.empty() &&
      catalog->direct_gpu && catalog->gpu &&
      catalog->gpu.get()==catalog->direct_gpu.get() &&
      catalog->source_models && catalog->source_material_candidates &&
      catalog->source_models->UsesGpuBackend(
          catalog->direct_gpu->OriginalModelUploader());
  if(!catalog || !catalog->device || !catalog->gpu ||
     (!catalog->resident_lookup && !catalog->source_models &&
      !catalog->verified_candidates.empty()) ||
     (catalog->verified_candidates.empty() &&
      catalog->verified_font_materials.empty() &&
      !source_residency_bootstrap) ||
     (!catalog->verified_font_materials.empty() &&
      (!catalog->direct_gpu ||
       catalog->gpu.get()!=catalog->direct_gpu.get())) ||
     !catalog->output.direct_engine_path_ready ||
     !native::HasFullNativeCompositePixels(
         catalog->output.world,catalog->output.composite,
         catalog->output.backbuffer))
    return false;
  // Catalogues are REUSABLE original engine material candidates, not
  // manually forged frames. Exact per-instance guest shader pointers and
  // frame/ordinal are acquired anew from the real Present inside the worker.
  for(const auto& item:catalog->verified_candidates) {
    const auto& owner=std::visit([](const auto& link)
        ->const native::VerifiedDrawSource& {return link.native_draw;},
        item.original_source_submission);
    if(!item.original_guest_resource_to_material_mapping_verified ||
       !owner.engine_draw_verified || !owner.mesh_material_verified ||
       !owner.host_vertex_layout_verified || !owner.render_state_verified ||
       !owner.original_model_hash || owner.original_surface_name.empty() ||
       owner.original_surface_ordinal==UINT32_MAX ||
       owner.original_frame_sequence ||
       owner.original_call_ordinal!=UINT32_MAX ||
       item.original_source_engine_shader_method)
      return false;
    if(item.original_shader_class_material_owner_verified &&
       (!item.original_material_shader_method_guest ||
        !item.original_material_shader_vtable_guest ||
        !ProvenUsaEorShaderMethodVtablePair(
            item.original_material_shader_method_guest,
            item.original_material_shader_vtable_guest)))
      return false;
  }
  // A genuine UI pass must have a source-proven XDK vertex declaration
  // and real atlas ownership; the display name or a logo texture is not
  // permission to invent a font input layout.
  for(const auto& font:catalog->verified_font_materials) {
    const auto& v=font.source_vertex_layout;
    if(!font.original_font_resource_hash ||
       font.original_surface_ordinal==UINT32_MAX ||
       !v.original_font_atlas_hash ||
       font.original_effect_hash!=ui::kOriginalFontMainEffectHash ||
       font.original_pass_index!=ui::kOriginalFontMainPassIndex ||
       !v.original_font_sampler_owner_verified ||
       v.original_pixel_texture_slot>=16 ||
       (!font.resolve_verified_live_vertex_layout && !ui::UsesLiveUsaFontDeclaration(v) &&
        (!v.original_vertex_declaration_guest ||
         !v.original_vertex_declaration_layout_verified ||
         !v.original_stride)))
      return false;
  }
  std::lock_guard lock(g_mutex);
  // Check source-pack/factory generation in the SAME critical section as
  // publication. Validating the callback under one lock and installing
  // later allowed a stale GPU catalog to overwrite a newer owner.
  if(!g_offline_shader_pack || !g_offline_shader_pack->virtual_ids ||
     (factory && (!*factory || g_worker_catalog_factory!=*factory)) ||
     (pack && (g_offline_shader_pack->shaders!=pack->shaders ||
               g_offline_shader_pack->effects!=pack->effects ||
               g_offline_shader_pack->reflection!=pack->reflection ||
               g_offline_shader_pack->samplers!=pack->samplers ||
               g_offline_shader_pack->virtual_ids!=pack->virtual_ids)) ||
     (expected_previous &&
      g_worker_owner_catalog!=*expected_previous))
    return false;
  // Private metadata-only ShaderEffects headers do not execute on Vulkan.
  // Both genuine independently compiled original Font_Main VS+PS modules
  // must be in the active HEORSPV1 source pack before any UI material is
  // eligible for native frame submission. Do not create a placeholder.
  if(!catalog->verified_font_materials.empty() &&
     (!g_offline_shader_pack->shaders ||
      !g_offline_shader_pack->effects ||
      !g_offline_shader_pack->shaders->Find(
          ui::kOriginalFontMainVsSha,aot::Stage::Vertex) ||
      !g_offline_shader_pack->shaders->Find(
          ui::kOriginalFontMainPsSha,aot::Stage::Pixel) ||
      !g_offline_shader_pack->effects->HasPair(
          ui::kOriginalFontMainEffectHash,ui::kOriginalFontMainPassIndex,
          ui::kOriginalFontMainVsSha,ui::kOriginalFontMainPsSha)))
    return false;
  g_verified_worker_frame.reset();
  g_worker_owner_catalog=std::move(catalog);
  return true;
}
} // namespace
bool InstallVerifiedWorkerEorOwnerCatalog(
    std::shared_ptr<const VerifiedWorkerEorOwnerCatalog> catalog) {
  return InstallVerifiedWorkerEorOwnerCatalogInternal(
      std::move(catalog),nullptr,nullptr,nullptr);
}
bool InstallVerifiedWorkerEorCatalogFactory(
    VerifiedWorkerEorCatalogFactory factory) {
  if(!factory)return false;
  auto verified_callback=
      std::make_shared<const VerifiedWorkerEorCatalogFactory>(
          std::move(factory));
  std::lock_guard lock(g_mutex);
  // A registered producer can be invoked on the next real source Present
  // with the actual initialized Vulkan Device, not a speculative handle
  // guessed at startup before NativeX device initialization.
  g_worker_catalog_factory=std::move(verified_callback);
  g_worker_catalog_factory_last_attempt=0;
  g_worker_owner_catalog.reset();
  g_verified_worker_frame.reset();
  return true;
}
void ClearVerifiedWorkerEorCatalogFactory() noexcept {
  std::lock_guard lock(g_mutex);
  g_worker_catalog_factory.reset();
  g_worker_catalog_factory_last_attempt=0;
  g_worker_owner_catalog.reset();
  g_verified_worker_frame.reset();
}
void ClearVerifiedWorkerEorOwnerCatalog() noexcept {
  std::lock_guard lock(g_mutex);
  g_worker_owner_catalog.reset();
  g_verified_worker_frame.reset();
  g_worker_catalog_factory_last_attempt=0;
}
void ClearVerifiedWorkerEorFrame() noexcept {
  std::lock_guard lock(g_mutex);
  g_verified_worker_frame.reset();
}
EorWorkerStatus GetEorWorkerStatus() noexcept {
  return {g_eor_ready_backbuffers.load(std::memory_order_relaxed),
          g_eor_gpu_failures.load(std::memory_order_relaxed)};
}
OriginalEorSourceCoverage GetOriginalEorSourceCoverage() noexcept {
  std::lock_guard lock(g_mutex);
  return g_original_source_coverage;
}
namespace {
// The mixed native scene accepts genuine EFontShader Draws in their exact
// original source order. Static original meshes continue to use the byte-
// exact MODELS.ARC witness resolver; transient font vertices use the verified
// XDK declaration, current source VB/IB and archived atlas, never a forged
// static model. Any unsupported Draw fails the ENTIRE frame before Clear.
struct PreparedEorMixedFrame {
  std::vector<aot::BoundIndexedDraw> ordered;
  std::vector<std::shared_ptr<const model::UploadedOriginalModel>> world;
};
// A single-target source frame is the first supported production path.
// Establish the actual original screen by its current guest target objects,
// full original clears and one final full-surface Resolve -> Present.
// Tiled, chained postprocess, MRT, partial resolve and unknown target formats
// are NOT equivalent to this path and must keep the compatibility presenter.
struct AutomaticSceneContract {
  native::FrameGeometry output;
  gpu::VerifiedSceneFormat format;
};
std::optional<AutomaticSceneContract> OriginalTargetContract(
    const Frame& frame,native::Size backbuffer,const OfflineShaderPack& pack) {
  const auto target=gpu::PlanOriginalNativeTargets(frame);
  if(!target)return {};
  AutomaticSceneContract result{};
  result.format=target->format;
  const auto& graph=result.format.original_graph;
  const auto source=graph.extents.at(graph.final_color);
  const double source_aspect=double(source.width)/source.height;
  const bool aspect_change=uint64_t(backbuffer.width)*source.height!=
                           uint64_t(backbuffer.height)*source.width;
  if(aspect_change && (!pack.shaders || !pack.reflection || !pack.effects || !pack.virtual_ids))return {};
  // All known source images have a single output. A distinct source RT is
  // never aliased to the main scene just because it is the same extent.
  // Non-screen-sized targets retain their exact original extent and
  // projection; screen-sized targets still require a proven view ABI.
  for(size_t ordinal=0;ordinal<frame.calls.size();++ordinal) {
    const auto& draw=frame.calls[ordinal];
    // The target planner can be reached from immutable frames whose first
    // missing shader prevented heavy register capture. A missing guest
    // device snapshot or projection constants is not a valid 16:10 camera
    // contract; reject before dereferencing either optional owner.
    if(!draw.observed_vs_matrices)
      return {};
    const auto& state=*draw.observed_vs_matrices;
    if(aspect_change) {
      if(!HasOriginalDrawProgram(draw))return {};
      const auto vs=aot::ResolveOriginalLiveXdkShaderSource(*pack.virtual_ids,
          *draw.observed_live_vertex_shader_header,state.original_vs_guest_object,
          aot::Stage::Vertex,frame.sequence,static_cast<uint32_t>(ordinal));
      if(!vs)return {};
      bool font_program=false;
      if(!IsVerifiedVertexOnlyDepthDraw(draw)) {
        const auto ps=aot::ResolveOriginalLiveXdkShaderSource(*pack.virtual_ids,
            *draw.observed_live_pixel_shader_header,state.original_ps_guest_object,
            aot::Stage::Pixel,frame.sequence,static_cast<uint32_t>(ordinal));
        const auto effect=*draw.observed_live_eor_effect_pass;
        if(!ps || !pack.effects->HasPair(effect.effect_hash,effect.pass_index,*vs,*ps))return {};
        font_program=ui::MatchesOriginalUsaFontMainProgram(effect.effect_hash,effect.pass_index,*vs,*ps);
      }
      if(font_program) {
        const auto font=draw.kind==Kind::EndVertices ? ui::ResolveUsaInlineFontLayout(draw) :
                                                      ui::ResolveUsaBufferedFontLayout(draw);
        if(!font)return {};
      } else if(!state.original_color_extent ||
                (state.original_color_extent->width==source.width && state.original_color_extent->height==source.height)) {
        const auto* shader=pack.shaders->Find(*vs,aot::Stage::Vertex);
        const auto* reflection=pack.reflection->Find(*vs,aot::Stage::Vertex);
        if(!shader || !reflection)return {};
        const bool clip_xyw_preserved=shader->scalar_preserves_clip_xyw ?
            *shader->scalar_preserves_clip_xyw :
            aot::ScalarPreservesClipXYW(shader->spirv);
        // A real Font_Main or already clip-space shader does not consume
        // camera matrices for 16:10 projection. Perspective world Draws do:
        // never dereference missing full registers or invent a FOV.
        if(!clip_xyw_preserved &&
           (!draw.observed_shader_constants ||
            !aot::IdentifyLiveScalarProjection(*shader,*reflection,
                *draw.observed_shader_constants,source_aspect)))
          return {};
      }
    }
  }
  native::FrameSetup setup;
  setup.backbuffer=backbuffer;setup.original_guest={source.width,source.height};
  setup.engine_draw_hook_verified=true;
  setup.offline_shader_cache_verified=true;
  setup.samples=target->samples;
  setup.projection_hook_verified=aspect_change;
  result.output=native::PlanFrame(setup);
  if(!result.output.direct_engine_path_ready ||
     !gpu::OriginalTargetGraphFitsBudget(result.format.original_graph,result.output))return {};
  return result;
}
bool SameAutomaticScene(const AutomaticSceneContract& a,
                        const AutomaticSceneContract& b) noexcept {
  const auto& x=a.format.original_screen;
  const auto& y=b.format.original_screen;
  return a.output.backbuffer.width==b.output.backbuffer.width &&
         a.output.backbuffer.height==b.output.backbuffer.height &&
         a.output.original_guest.width==b.output.original_guest.width &&
         a.output.original_guest.height==b.output.original_guest.height &&
         a.output.samples==b.output.samples &&
         a.format.color==b.format.color && a.format.depth==b.format.depth &&
         x.guest_device==y.guest_device && x.guest_rt0==y.guest_rt0 &&
         x.guest_depth==y.guest_depth &&
         gpu::SameOriginalTargetStorage(a.format.original_graph,b.format.original_graph);
}
struct AutomaticArchivePaths {
  std::filesystem::path index,models,textures,streams;
};
std::optional<AutomaticArchivePaths> FindAutomaticArchives(
    const std::filesystem::path& root) {
  if(root.empty())return {};
  const auto base=root/"DATA"/"XENON";
  // The game-data resolver can return the original root OR its DATA or
  // XENON directory. Look in the actual owner layout, including the
  // separately supplied, digest-verified archive index.
  const auto xenon=root/"XENON";
  for(const auto& index:{base/"INDEX.IND",base/"indexXBOX360.ind",
       base/"USENGLISH"/"INDEX.IND",base/"UKENGLISH"/"INDEX.IND",
       xenon/"INDEX.IND",xenon/"indexXBOX360.ind",
       xenon/"USENGLISH"/"INDEX.IND",xenon/"UKENGLISH"/"INDEX.IND",
       root/"USENGLISH"/"INDEX.IND",root/"UKENGLISH"/"INDEX.IND",
       root/"INDEX.IND",root/"indexXBOX360.ind",root/"index(1).ind",root/"index(2).ind",
       root/"Data"/"Xenon"/"INDEX.IND"}) {
    if(!std::filesystem::is_regular_file(index))continue;
    for(const auto& dir:{index.parent_path(),base,xenon,root}) {
      const auto models=dir/"MODELS.ARC";
      const auto textures=dir/"TEXTURES.ARC";
      if(!std::filesystem::is_regular_file(models) ||
         !std::filesystem::is_regular_file(textures))continue;
      std::filesystem::path streams;
      for(const auto& candidate:{dir/"TEXSTREAMS.ARC",dir/"TEXSTREAM.ARC",dir/"TexStreams.arc"})
        if(std::filesystem::is_regular_file(candidate)){streams=candidate;break;}
      try {
        // Both owner indices have been independently compared at the
        // complete graphics table level, including TexStreams. The
        // second pinned source index is NOT a regional guess.
        (void)Index::LoadPinnedUsaGraphicsArchive(index);
        return AutomaticArchivePaths{index,models,textures,streams};
      } catch(const std::exception&) {}
    }
  }
  return {};
}
// Archive identity, immutable GPU mesh allocations, texture residency and
// compiled source shader modules are independent of the current screen RT
// graph. Share them across menu/gameplay transitions and native resizes.
// The scene/backend itself is deliberately NOT shared: its target and PSO
// witnesses must be rebuilt for the exact next original command timeline.
struct AutomaticEorResources {
  Diligent::IRenderDevice* device;
  OfflineShaderPack pack;
  std::filesystem::path root,candidate_file;
  gpu::DiligentMeshUploadBackend meshes;
  gpu::DiligentAotShaderModules shaders;
  textures::OriginalXboxTextureAssets assets;
  gpu::DiligentOriginalTextureResidency textures;
  std::shared_ptr<world::NativeWorldStream> source_models;
  std::shared_ptr<world::OriginalMaterialCandidateIndex> candidates;
  gpu::live::ScalarAnalysisCache scalar_analysis;

  AutomaticEorResources(Diligent::IRenderDevice* render_device,OfflineShaderPack source,
      const AutomaticArchivePaths& paths,const std::filesystem::path& source_root,
      const std::filesystem::path& candidate_path)
      :device(render_device),pack(std::move(source)),root(source_root),
       candidate_file(candidate_path),meshes(render_device),shaders(render_device),
       assets(textures::OriginalXboxTextureAssets::OpenPinnedUsa(paths.index,paths.textures,paths.streams)),
       textures(render_device,assets) {
    if(!pack.shaders || !pack.effects || !pack.reflection ||
       !pack.virtual_ids || !pack.samplers)
      throw std::invalid_argument("Incomplete live EoR source resources");
    source_models=std::make_shared<world::NativeWorldStream>(
        std::make_unique<world::ArchiveModelSource>(paths.index,paths.models),
        meshes,std::vector<world::LodTier>{});
    candidates=std::make_shared<world::OriginalMaterialCandidateIndex>();
    bool cached=false;
    if(!candidate_path.empty() && std::filesystem::is_regular_file(candidate_path)) {
      try {
        candidates->Load(candidate_path);
        constexpr std::array<uint8_t,32> pinned{
          0x46,0xe6,0xc6,0xec,0x9f,0x74,0x28,0xba,0x3a,0x17,0x37,0xab,0x7e,0xba,0xd5,0x64,
          0xc1,0xb0,0x27,0xb0,0x06,0xc3,0xe3,0x51,0x56,0xd2,0xe0,0x36,0xab,0x8b,0x28,0x20};
        cached=candidates->OriginalIndexDigest()==pinned && candidates->LinkCount()==41745u;
      } catch(const std::exception&) {}
    }
    if(!cached)candidates->LoadFromOriginalXboxArchive(paths.index,paths.models);
  }
  bool MatchesArchiveDevice(Diligent::IRenderDevice* render_device,
      const std::filesystem::path& source_root,
      const std::filesystem::path& candidate_path) const {
    return render_device && device==render_device && root==source_root &&
        candidate_file==candidate_path;
  }
  bool MatchesShaderPack(const OfflineShaderPack& source) const noexcept {
    return pack.shaders==source.shaders && pack.effects==source.effects &&
        pack.reflection==source.reflection && pack.samplers==source.samplers &&
        pack.virtual_ids==source.virtual_ids;
  }
  bool Matches(Diligent::IRenderDevice* render_device,const OfflineShaderPack& source,
      const std::filesystem::path& source_root,
      const std::filesystem::path& candidate_path) const {
    return MatchesArchiveDevice(render_device,source_root,candidate_path) &&
        MatchesShaderPack(source);
  }
};
// A weak reference cannot retain resources beyond their owning GPU
// catalogue; only the existing sole Vulkan worker may read or replace it.
thread_local std::weak_ptr<AutomaticEorResources> g_automatic_resources;

// A per-owner GPU timeline replaces the forced vkQueueWaitIdle on EACH
// correctly submitted EoR frame. Every dynamic font/physics debris Draw
// keeps its original host VB/IB, PSO and shader bindings alive until the
// actual Vulkan fence completes. This is not an elapsed-frame heuristic.
struct OriginalEorGpuCompletion {
  Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
  Diligent::RefCntAutoPtr<Diligent::IFence> fence;
  gpu::DiligentEorDirectBackend* backend=nullptr; // owned by AutomaticEorOwner
  std::deque<std::pair<uint64_t,uint64_t>> in_flight; // fence value, source frame
  uint64_t last_signal=0;
  OriginalEorGpuCompletion(Diligent::IRenderDevice* device,
      Diligent::IDeviceContext* worker,gpu::DiligentEorDirectBackend* gpu)
      :context(worker),backend(gpu) {
    if(device) {
      Diligent::FenceDesc desc{};
      desc.Name="Original EoR native in-flight Draw retirement";
      desc.Type=Diligent::FENCE_TYPE_CPU_WAIT_ONLY;
      // A backend without fences falls back to the existing completion
      // barrier. It must never fake GPU completion to release live VBs.
      try {device->CreateFence(desc,&fence);} catch (...) {fence.Release();}
    }
  }
  void Poll() {
    if(!fence || !backend)return;
    const auto completed=fence->GetCompletedValue();
    uint64_t safe=0;
    while(!in_flight.empty() && in_flight.front().first<=completed) {
      safe=in_flight.front().second;
      in_flight.pop_front();
    }
    if(safe)backend->RetireCompletedOriginalFontDraws(safe);
  }
  bool Submit(uint64_t original_frame) {
    if(!context || !backend || !original_frame)return false;
    if(!fence) {
      context->Flush();
      context->WaitForIdle();
      backend->RetireCompletedOriginalFontDraws(original_frame);
      return true;
    }
    if(last_signal==UINT64_MAX)return false;
    // The worker and its immediate Vulkan context are single-threaded.
    // Signal AFTER the last EoR Draw/Resolve/Composite, flush and check
    // actual completion only; never free by source Present sequence alone.
    const uint64_t value=last_signal+1;
    context->EnqueueSignal(fence,value);
    context->Flush();
    last_signal=value;
    in_flight.emplace_back(value,original_frame);
    Poll();
    if(in_flight.size()>3) {
      // Wait only for the oldest queued Draw resources to become reusable.
      fence->Wait(in_flight.front().first);
      Poll();
    }
    return true;
  }
  ~OriginalEorGpuCompletion() noexcept {
    if(in_flight.empty() || !context)return;
    try {
      context->Flush();
      context->WaitForIdle();
      Poll(); // original backend is still alive: member destruction order
    } catch (...) {
      // On device loss keep references until the existing device teardown.
    }
  }
};
struct OriginalFrameGpu {
  native_x_renderer::Device* worker=nullptr;
#ifdef _WIN32
  gpu::EorWin32VulkanHost* window=nullptr;
#endif
  Diligent::IRenderDevice* device() const noexcept {
#ifdef _WIN32
    if(window)return window->Device();
#endif
    return worker->device();
  }
  Diligent::IDeviceContext* context() const noexcept {
#ifdef _WIN32
    if(window)return window->Context();
#endif
    return worker->context();
  }
  Diligent::ISwapChain* swap_chain() const noexcept {
#ifdef _WIN32
    if(window)return window->SwapChain();
#endif
    return worker->swap_chain();
  }
};
struct AutomaticEorOwner {
  OriginalFrameGpu worker_host;
  Diligent::ISwapChain* const owner_swapchain;
  std::shared_ptr<AutomaticEorResources> resources;
  const OfflineShaderPack& pack;
  AutomaticSceneContract contract;
  gpu::DiligentSceneTargets scene;
  gpu::DiligentMeshUploadBackend& meshes;
  gpu::DiligentAotShaderModules& shaders;
  gpu::DiligentOriginalTextureResidency& textures;
  std::unique_ptr<gpu::DiligentEorHostOutput> worker_output;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> shared_constants;
  std::shared_ptr<world::NativeWorldStream> source_models;
  std::shared_ptr<world::OriginalMaterialCandidateIndex> candidates;
  std::unique_ptr<gpu::DiligentEorDirectBackend> backend;
  std::unique_ptr<OriginalEorGpuCompletion> gpu_completion;
#ifdef _WIN32
  std::unique_ptr<gpu::EorNativeFrameExecutor> native_executor;
#endif
  gpu::live::ScalarAnalysisCache& scalar_analysis;
  uint64_t checked_sequence=0;
  native::Size checked_size{};
  bool checked_accepts=false,checked_rebuild=false;

  AutomaticEorOwner(OriginalFrameGpu host,
      std::shared_ptr<AutomaticEorResources> source,AutomaticSceneContract source_contract)
      :worker_host(host),owner_swapchain(host.swap_chain()),
       resources(std::move(source)),pack(resources->pack),contract(std::move(source_contract)),
       scene(host.device(),host.context()),meshes(resources->meshes),
       shaders(resources->shaders),textures(resources->textures),
       source_models(resources->source_models),candidates(resources->candidates),
       scalar_analysis(resources->scalar_analysis) {
    gpu::DiligentEorDirectBackend::CompositeOutput composite;
#ifdef _WIN32
    if(host.window) {
      composite=[window=host.window](Diligent::ITextureView* color,native::Rect rect,native::Size size) {
        return window->CompositeOriginalColor(color,rect,size,rex::cvar::Query<bool>("vsync"));
      };
    } else
#endif
    {
      worker_output=std::make_unique<gpu::DiligentEorHostOutput>(
          *host.device(),*host.context(),*host.swap_chain());
      if(!worker_output->Initialize())
        throw std::invalid_argument("Incomplete live EoR scene output");
      composite=gpu::BindNativeEorVulkanBackbuffer(*worker_output);
    }
    std::array<uint32_t,8> native_values{};
    Diligent::BufferDesc desc{};
    desc.Name="Hulk scalar host ABI constants";
    desc.BindFlags=Diligent::BIND_UNIFORM_BUFFER;
    desc.Usage=Diligent::USAGE_DYNAMIC;
    desc.CPUAccessFlags=Diligent::CPU_ACCESS_WRITE;
    desc.Size=sizeof(native_values);
    host.device()->CreateBuffer(desc,nullptr,&shared_constants);
    if(!shared_constants)throw std::runtime_error("Scalar Vulkan shared UBO unavailable");
    auto bind=[this,context=host.context()](Diligent::IShaderResourceBinding* binding,
        const aot::ReflectedShader&,const aot::ReflectedShader&,
        const native::IndexedDraw&,const aot::OriginalDrawVsMatrices&,
        const xdk::OriginalShaderRegisters& registers,std::span<const float>,std::span<const float>) {
      if(!binding || !shared_constants || !context || !registers.fixed_graphics_state)return false;
      const auto alpha=gpu::ResolveOriginalScalarAlpha(*registers.fixed_graphics_state);
      if(!alpha)return false;
      // A shader with no shared uniform block does not consume these bytes.
      // Avoid allocating dynamic uniform storage for such draws, especially
      // the original depth-only prepass. Source validation above is unchanged.
      auto* vertex_shared=binding->GetVariableByName(Diligent::SHADER_TYPE_VERTEX,"HulkSharedConstants");
      auto* pixel_shared=binding->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,"HulkSharedConstants");
      if(!vertex_shared && !pixel_shared)return true;
      std::array<uint32_t,8> shared{};
      if(const auto bits=PackOriginalScalarBooleans(registers))shared[0]=*bits;
      shared[4]=std::bit_cast<uint32_t>(alpha->threshold);
      shared[5]=alpha->flags;
      void* mapped=nullptr;
      context->MapBuffer(shared_constants,Diligent::MAP_WRITE,Diligent::MAP_FLAG_DISCARD,mapped);
      if(!mapped)return false;
      std::memcpy(mapped,shared.data(),sizeof(shared));
      context->UnmapBuffer(shared_constants,Diligent::MAP_WRITE);
      for(auto* var:{vertex_shared,pixel_shared})
        if(var && var->Get()!=shared_constants.RawPtr())
          var->Set(shared_constants.RawPtr(),Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      return true;
    };
    backend=std::make_unique<gpu::DiligentEorDirectBackend>(
        host.device(),host.context(),scene,meshes,shaders,*pack.shaders,*pack.effects,
        contract.output,contract.format,std::vector<gpu::NativeEorPassResources>{},
        std::vector<gpu::NativeEorDrawBuffers>{},std::move(bind),
        std::move(composite));
#ifdef _WIN32
    if(host.window)
      native_executor=std::make_unique<gpu::EorNativeFrameExecutor>(
          *host.window,*backend,*pack.shaders,*pack.effects,*pack.reflection);
    else native_executor=std::make_unique<gpu::EorNativeFrameExecutor>(
        host.device(),host.context(),host.swap_chain(),*backend,
        *pack.shaders,*pack.effects,*pack.reflection);
#else
    gpu_completion=std::make_unique<OriginalEorGpuCompletion>(
        host.device(),host.context(),backend.get());
#endif
  }
  bool SameWorkerHandles() const noexcept {
    return owner_swapchain && owner_swapchain==worker_host.swap_chain() &&
        backend && backend->UsesNativeHostGpu(worker_host.device(),worker_host.context());
  }
  bool Accepts(const Frame& frame,native::Size size) {
    // Same HWND/extent is not proof that the Vulkan owner survived a reset.
    // Rebuild the catalog before dereferencing a replaced swapchain.
    if(!SameWorkerHandles()) {
      checked_accepts=false;checked_rebuild=true;return false;
    }
    // Front-buffer rotation can be waiting for transient owners from the
    // last submission. Poll before deciding whether to accept the next
    // frame; a rejected frame never reaches Submit's retirement poll.
#ifdef _WIN32
    if(native_executor)native_executor->RetireCompleted();
#else
    if(gpu_completion)gpu_completion->Poll();
#endif
    // A claimed source frame is immutable. Factory refresh, early worker
    // validation and final submission must not repeatedly decode its full
    // Draw/Resolve graph and inspect all source projection shaders.
    if(checked_sequence && checked_sequence==frame.sequence &&
       checked_size.width==size.width && checked_size.height==size.height)
      return checked_accepts;
    checked_sequence=frame.sequence;
    checked_size=size;
    checked_accepts=false;
    checked_rebuild=false;
    const auto live=OriginalTargetContract(frame,size,pack);
    if(!live)return false;
    checked_rebuild=!SameAutomaticScene(contract,*live);
    if(checked_rebuild || !backend->RebindOriginalFrontBuffer(live->format))
      return false;
    contract=*live;
    checked_accepts=true;
    return true;
  }
  std::vector<aot::BoundIndexedDraw> Prepare(const Frame& frame,
      std::vector<std::shared_ptr<const model::UploadedOriginalModel>>& retained) {
    std::map<std::string,std::string,std::less<>> pc_bindings;
    if(ui::PcPromptInputActive()) {
      for(const char* key:{"hulk_bind_confirm","hulk_bind_back","hulk_bind_primary",
          "hulk_bind_secondary","hulk_bind_lt","hulk_bind_rt","hulk_bind_lb",
          "hulk_bind_rb","hulk_bind_start","hulk_bind_select","hulk_bind_menu_up",
          "hulk_bind_menu_down","hulk_bind_menu_left","hulk_bind_menu_right"})
        pc_bindings.emplace(key,rex::cvar::Query<std::string>(key));
    }
    if(!backend->SetHostPresentationOptions(rex::cvar::Query<int32_t>("anisotropic_override"),
                                            std::move(pc_bindings)))
      throw std::runtime_error("Native host settings cannot change during an active scene");
    std::vector<aot::BoundIndexedDraw> ordered;
    ordered.reserve(frame.calls.size());
    // Strong owners remain attached to this source frame until the
    // existing worker's Vulkan queue completion barrier.
    std::map<uint32_t,std::shared_ptr<const model::UploadedOriginalModel>> models;
    std::set<size_t> runtime_required;
    std::set<uint32_t> requested;
    // Multiple city draws can sample the same immutable archive texture
    // set. Cache the intersection for this frame only. This is strictly
    // a prefetch hint: geometry, shader and material still require their
    // own exact per-Draw evidence in ProduceOriginalWorldMaterial.
    using TextureSetKey=std::pair<uint16_t,std::array<uint32_t,16>>;
    std::map<TextureSetKey,std::vector<world::OriginalSurfaceCandidate>> frame_candidates;
    // Discover only source-proven texture candidates. Request ALL unique
    // original archive owners for the sampled texture set (bounded).
    // A unique match among an incomplete subset is not archive proof.
    for(size_t ordinal=0;ordinal<frame.calls.size();++ordinal) {
      const auto& call=frame.calls[ordinal];
      const auto& parent=call.original_engine_shader_method;
      if(parent && parent->original_shader_method_guest==0x8266a0c0u)continue;
      if(!parent || !call.observed_live_pixel_shader_header ||
         !call.observed_live_pixel_shader_header->pixel_sampling ||
         !parent->original_pass_sampler_slots_verified)
        {runtime_required.insert(ordinal);continue;}
      auto mask=call.observed_live_pixel_shader_header->pixel_sampling->sampled_slots;
      if(call.observed_vs_matrices)
        for(uint32_t slot=0;slot<16;++slot)
          if(contract.format.original_graph.HasResolve(call.observed_vs_matrices->original_texture_guest_objects[slot]))
            mask&=static_cast<uint16_t>(~(1u<<slot));
      if(!mask || (mask&~parent->original_pass_sampler_slot_mask))
        {runtime_required.insert(ordinal);continue;}
      auto keys=call.original_pixel_texture_keys;
      for(uint32_t slot=0;slot<16;++slot)
        if(!(mask&(1u<<slot)))keys[slot]=0;
      auto [entry,inserted]=frame_candidates.try_emplace(TextureSetKey{mask,keys});
      if(inserted)entry->second=candidates->FindLiveSurfaceCandidates(keys,mask,0,96);
      const auto& surfaces=entry->second;
      if(surfaces.empty()){runtime_required.insert(ordinal);continue;}
      for(const auto& surface:surfaces) {
        const auto hash=surface.original_model_hash;
        if(models.contains(hash))continue;
        // A missing resident cannot become visible until the next worker
        // upload tick. Reuse this frame's negative result for every surface.
        if(requested.contains(hash)){runtime_required.insert(ordinal);continue;}
        auto resident=source_models->ResidentShared(hash);
        if(!resident){runtime_required.insert(ordinal);requested.insert(hash);continue;}
        models.emplace(hash,std::move(resident));
      }
    }
    if(!requested.empty()) {
      const std::vector<uint32_t> queue(requested.begin(),requested.end());
      source_models->RequestSourceVerifiedModels(queue);
    }
    std::vector<const model::UploadedOriginalModel*> borrowed;
    for(const auto& [_,model]:models) {
      retained.push_back(model);borrowed.push_back(model.get());
    }
    if(!borrowed.empty())backend->AdoptVerifiedResidentWorldMeshes(borrowed);
    native::FrameMeshLookup mesh_lookup(borrowed);
    for(size_t i=0;i<frame.calls.size();++i) {
      const auto& call=frame.calls[i];
      const auto ordinal=static_cast<uint32_t>(i);
      const auto& parent=call.original_engine_shader_method;
      if(parent && parent->original_shader_method_guest==0x8266a0c0u) {
        auto layout=call.kind==Kind::EndVertices ? ui::ResolveUsaInlineFontLayout(call) :
                                                  ui::ResolveUsaBufferedFontLayout(call);
        if(!layout || !layout->color || !call.observed_vs_matrices ||
           !call.observed_live_vertex_shader_header || !call.observed_live_pixel_shader_header ||
           !call.observed_shader_constants ||
           !call.observed_shader_constants->fixed_graphics_state)
          throw std::invalid_argument("Live original font material or fixed-state registers unavailable");
        const auto& live=*call.observed_vs_matrices;
        const auto vs=aot::ResolveOriginalLiveXdkShaderSource(*pack.virtual_ids,
             *call.observed_live_vertex_shader_header,live.original_vs_guest_object,
             aot::Stage::Vertex,frame.sequence,ordinal);
        const auto ps=aot::ResolveOriginalLiveXdkShaderSource(*pack.virtual_ids,
             *call.observed_live_pixel_shader_header,live.original_ps_guest_object,
             aot::Stage::Pixel,frame.sequence,ordinal);
        if(!vs || !ps || !call.observed_live_eor_effect_pass ||
           !ui::MatchesOriginalUsaFontMainProgram(call.observed_live_eor_effect_pass->effect_hash,
              call.observed_live_eor_effect_pass->pass_index,*vs,*ps) ||
           !pack.effects->HasPair(ui::kOriginalFontMainEffectHash,ui::kOriginalFontMainPassIndex,*vs,*ps))
          throw std::invalid_argument("Original live Font_Main shader identity unavailable");
        const auto* v=pack.shaders->Find(*vs,aot::Stage::Vertex);
        const auto* p=pack.shaders->Find(*ps,aot::Stage::Pixel);
        if(!v || !p || !scalar_analysis.Get(*pack.shaders,*vs,aot::Stage::Vertex).interface.supported ||
           !scalar_analysis.Get(*pack.shaders,*ps,aot::Stage::Pixel).interface.supported)
          throw std::invalid_argument("Original Font_Main scalar ABI unsupported");
        // Dynamic EFont has no model/surface archive ordinal. Its actual
        // atlas texture resource is the material owner; subresource zero
        // is the only atlas binding, not an invented MODELS.ARC record.
        const auto atlas=layout->original_font_atlas_hash;
        gpu::VerifiedNativeMaterialPass material;
        material.original_model_hash=atlas;material.original_surface_ordinal=0;
        auto& pass=material.original_pipeline;
        pass=gpu::live::OriginalScalarPass(call,*vs,*ps,contract.format);
        using namespace Diligent;
        pass.exact_host_input_layout={{0,0,layout->position.components,VT_FLOAT32,False},
                                      {4,1,layout->texcoord.components,VT_FLOAT32,False},
                                      {8,2,layout->color->components,VT_FLOAT32,False}};
        for(auto& element:pass.exact_host_input_layout)element.Stride=16u;
        const auto& pixel_interface=scalar_analysis.Get(*pack.shaders,*ps,aot::Stage::Pixel).interface;
        pass.original_scalar_alpha_test_verified=pixel_interface.has_kill &&
            (pixel_interface.shared_members&24u)==24u;
        const auto alpha=gpu::ResolveOriginalScalarAlpha(*call.observed_shader_constants->fixed_graphics_state);
        if(!alpha || (alpha->flags && !pass.original_scalar_alpha_test_verified))
          throw std::invalid_argument("Original Font_Main alpha test unsupported");
        pass.original_dynamic_font_vertex_layout_verified=true;
        pass.original_host_vertex_layout_verified=true;
        pass.original_view_space=gpu::VerifiedOriginalViewSpace::OriginalAspectHud;
        pass.original_view_space_verified=pass.original_hud_clip_coordinates_verified=true;
        material.original_images=gpu::live::OriginalScalarImages(call,*vs,*ps,
            *pack.shaders,*pack.samplers,atlas,0,1u,textures,contract.format,&scalar_analysis);
        pass.material_pass_verified=true;
        backend->AdoptVerifiedOriginalMaterialPasses({&material,1},textures);
        ordered.push_back(backend->StageVerifiedOriginalFontDraw(
            call,*layout,frame.sequence,ordinal,atlas,0,*pack.virtual_ids));
      } else {
        // Prefer exact resident archive geometry. A transient Draw is
        // independently verified against its actual declaration, VB/IB and
        // original material setter, not treated as an approximate archive match.
        std::optional<gpu::live::ProducedWorldMaterial> archived;
        if(!IsVerifiedVertexOnlyDepthDraw(call) && !runtime_required.contains(i)) {
          try {
            archived=gpu::live::ProduceOriginalWorldMaterial(frame,ordinal,
                borrowed,mesh_lookup,*pack.shaders,*pack.effects,*pack.virtual_ids,*pack.samplers,
                contract.format,textures,&scalar_analysis);
          } catch(const std::invalid_argument&) {}
        }
        auto project=[&](gpu::VerifiedOriginalGraphicsPass& pipeline,const aot::Digest& digest) {
          // View classification also controls PC key art and world AF in
          // 16:9. Only the requirement for projection expansion depends
          // on whether the physical output changes the original aspect.
          const auto& extent=call.observed_vs_matrices->original_color_extent;
          const auto source=contract.output.original_guest;
          const double source_aspect=double(source.width)/source.height;
          if(extent && (extent->width!=source.width || extent->height!=source.height)) {
            pipeline.original_view_space=gpu::VerifiedOriginalViewSpace::Offscreen;
            pipeline.original_view_space_verified=true;
            return;
          }
          const auto* vertex=pack.shaders->Find(digest,aot::Stage::Vertex);
          const auto* reflected=pack.reflection->Find(digest,aot::Stage::Vertex);
          const auto projection=vertex && reflected && call.observed_shader_constants ?
              aot::IdentifyLiveScalarProjection(*vertex,*reflected,*call.observed_shader_constants,source_aspect) :
              std::nullopt;
          if(!projection) {
            if(!vertex || !(vertex->scalar_preserves_clip_xyw ? *vertex->scalar_preserves_clip_xyw :
                aot::ScalarPreservesClipXYW(vertex->spirv))) {
              if(contract.output.widescreen_camera)
                throw std::invalid_argument("Live perspective or clip-space camera matrix not proven");
              return; // exact original aspect needs no projection change
            }
            // Geometry is already in clip coordinates. Preserve its original
            // picture aspect unless this is a proven full-screen post pass.
            pipeline.original_view_space=gpu::VerifiedOriginalViewSpace::OriginalAspectHud;
            pipeline.original_view_space_verified=true;
            pipeline.original_hud_clip_coordinates_verified=true;
            const auto& live=*call.observed_vs_matrices;
            const auto& raster=live.raster;
            if(raster && raster->x==0 && raster->y==0 &&
               raster->width==float(source.width) && raster->height==float(source.height) && !raster->scissor_enabled &&
               call.observed_live_pixel_shader_header &&
               call.observed_live_pixel_shader_header->pixel_sampling) {
              const auto sampled=call.observed_live_pixel_shader_header->pixel_sampling->sampled_slots;
              bool resolved=false;
              for(uint32_t slot=0;slot<16;++slot)
                if((sampled&(1u<<slot)) && contract.format.original_graph.HasResolve(live.original_texture_guest_objects[slot]))
                  resolved=true;
              // A fullscreen post pass must actually cover the entire
              // original viewport. Do not infer that from its effect name.
              if(resolved && call.captured_vertex_bindings) {
                try {
                  const auto decoded=gpu::live::DecodeOriginalRuntimeGeometry(call,scalar_analysis.Get(*pack.shaders,digest,aot::Stage::Vertex).interface);
                  if(gpu::live::CoversOriginalClipQuad(decoded))
                    pipeline.original_view_space=gpu::VerifiedOriginalViewSpace::FullOutputPost;
                } catch(const std::exception&) {}
              }
            }
            return;
          }
          pipeline.original_view_space=gpu::VerifiedOriginalViewSpace::MainPerspective;
          pipeline.original_view_space_verified=true;
          pipeline.source_main_view=gpu::VerifiedOriginalMainView{
              projection->proof,projection->registers.index,source_aspect,true};
        };
        if(IsVerifiedVertexOnlyDepthDraw(call)) {
          auto produced=gpu::live::ProduceOriginalVertexOnlyMaterial(call,frame.sequence,ordinal,
              *pack.shaders,pack.virtual_ids,contract.format,scalar_analysis);
          project(produced.material.original_pipeline,produced.material.original_pipeline.original_vertex_shader_sha256);
          ordered.push_back(backend->StageOriginalRuntimeDraw(call,frame.sequence,ordinal,
              produced.material,produced.geometry,textures));
        } else {
          // An archive match is a fast path, not the only native geometry
          // representation. A genuine streamed surface can change shader
          // pair, layout, or image ownership while its archive hash stays
          // constant. Admission checks the existing resident generation
          // BEFORE inserting this single material. If it rejects, keep
          // that resident material intact and independently prove the
          // actual current Draw using the existing transient producer.
          if(archived) {
            try {
              auto& produced=*archived;
              project(produced.material.original_pipeline,produced.draw.vertex_source_sha256);
              backend->AdoptVerifiedOriginalMaterialPasses({&produced.material,1},textures);
              ordered.push_back(std::move(produced.draw));
              continue;
            } catch(const std::invalid_argument&) {
              // No draw or Present has occurred. Do not suppress allocation
              // failures or relax the transient source/texture/PSO proof.
              archived.reset();
            }
          }
          auto produced=gpu::live::ProduceOriginalRuntimeMaterial(call,frame.sequence,ordinal,
              *pack.shaders,*pack.effects,*pack.virtual_ids,*pack.samplers,contract.format,textures,&scalar_analysis);
          project(produced.material.original_pipeline,produced.material.original_pipeline.original_vertex_shader_sha256);
          ordered.push_back(backend->StageOriginalRuntimeDraw(call,frame.sequence,ordinal,
              produced.material,produced.geometry,textures));
        }
      }
    }
    return ordered;
  }
};
// Upload and publish on the same context on which the mesh was requested.
void AdvanceOriginalMeshUploads(world::NativeWorldStream& stream,
                                gpu::DiligentEorDirectBackend& backend) {
  if(!stream.UsesGpuBackend(backend.OriginalModelUploader()))
    throw std::logic_error("Original mesh stream belongs to a different GPU");
  stream.TickAndPublishSourceVerifiedModels(4u,std::chrono::milliseconds(2),
      [&](const auto& resident) {
    if(!resident->original_resource_hash ||
       resident->original_identity.platform!=model::Platform::Xbox360)
      throw std::logic_error("Invalid original resident world model");
    const model::UploadedOriginalModel* mesh=resident.get();
    backend.AdoptVerifiedResidentWorldMeshes({&mesh,1});
  });
}

void RecordObservedOriginalAotPairDemand(const Frame& snapshot) noexcept;
bool AdoptNewPrivateOriginalSpirvIfSafe(uint64_t sequence,
    Diligent::IDeviceContext* context) noexcept;

#ifdef _WIN32
class ExclusiveEorSession : public std::enable_shared_from_this<ExclusiveEorSession> {
 public:
  std::unique_ptr<gpu::EorWin32VulkanHost> host;
  std::shared_ptr<AutomaticEorResources> resources;
  std::unique_ptr<AutomaticEorOwner> owner;
  AutomaticArchivePaths archives;
  std::filesystem::path root,candidate;
  std::function<bool(std::function<void()>)> post;
  bool queued=false; // guarded by g_mutex

  void NotifyLocked() noexcept {
    if(queued)return;
    try {
      queued=true;
      if(!post([weak=weak_from_this()] {if(auto self=weak.lock())self->Drain();}))
        queued=false;
    } catch (...) {queued=false;}
  }
 private:
  void ReportRejected(uint64_t sequence,const char* reason) noexcept {
    try {
      // Log transitions, including late first failures; never terminate
      // the noexcept window callback because logging allocated memory.
      if(last_rejection_!=reason || sequence<=5 || sequence%120==0) {
        REXLOG_WARN("[HULK/EOR-WIN32] frame={} not presented: {}",sequence,reason);
        last_rejection_=reason;
      }
    } catch (...) {}
  }
  void Drain() noexcept {
    std::shared_ptr<const Frame> frame;
    {
      std::lock_guard lock(g_mutex);
      queued=false;
      const auto latest=g_frames_for_native.Peek();
      if(!latest)return;
      if(latest->dropped || latest->dropped_commands) {
        ReportRejected(latest->sequence,"dropped original commands");
        return;
      }
      frame=g_frames_for_native.ClaimLatestIndependentIfSequence(latest->sequence);
      if(!frame)ReportRejected(latest->sequence,"source frame not independently claimable");
    }
    if(!frame)return;
    bool staged=false;
    try {
      if(!host || !host->Device() || !host->Context() || !host->SwapChain()) {
        ReportRejected(frame->sequence,"native host device unavailable");
        return;
      }
      RecordObservedOriginalAotPairDemand(*frame);
      AdoptNewPrivateOriginalSpirvIfSafe(frame->sequence,host->Context());
      std::optional<OfflineShaderPack> current;
      {
        std::lock_guard lock(g_mutex);
        current=g_offline_shader_pack;
      }
      if(!current || !current->shaders || !current->effects ||
         !current->reflection || !current->samplers || !current->virtual_ids) {
        ReportRejected(frame->sequence,"verified shader bank or metadata unavailable");
        return;
      }
      if(frame->calls.empty()) {
        ReportRejected(frame->sequence,"no original XDK Draw calls");
        return;
      }
      const auto missing=std::find_if_not(frame->calls.begin(),frame->calls.end(),HasRequiredReplaySnapshot);
      if(missing!=frame->calls.end()) {
        const auto ordinal=static_cast<size_t>(missing-frame->calls.begin());
        const auto reason="incomplete XDK Draw snapshot at ordinal "+std::to_string(ordinal)+
            " of "+std::to_string(frame->calls.size())+" kind="+
            std::to_string(static_cast<unsigned>(missing->kind));
        ReportRejected(frame->sequence,reason.c_str());
        return;
      }
      if(!ValidateOriginalCommandTimeline(*frame)) {
        ReportRejected(frame->sequence,"incomplete original XDK command timeline");
        return;
      }
      if(!host->Device() || !host->Context() || !host->SwapChain()) {
        ReportRejected(frame->sequence,"native host device unavailable");
        return;
      }
      const auto update=gpu::SelectNativeResourceUpdate(bool(resources),
          resources && resources->MatchesArchiveDevice(host->Device(),root,candidate),
          resources && resources->MatchesShaderPack(*current));
      if(update==gpu::NativeResourceUpdate::Recreate) {
        // Destroy the executor before its GPU and archive owners. Resources
        // allocated on a previous VkDevice cannot be reused on a replacement.
        owner.reset();
        resources.reset();
        resources=std::make_shared<AutomaticEorResources>(
            host->Device(),*current,archives,root,candidate);
      } else if(update==gpu::NativeResourceUpdate::ReplaceShaderPack) {
        // Same device and archive: retain immutable meshes/textures only.
        owner.reset();
        resources->shaders.Clear();
        resources->pack=*current;
      }
      if(!host->ResizeToClient()) {
        ReportRejected(frame->sequence,"host swapchain resize unavailable");
        return;
      }
      const auto& swap=host->SwapChain()->GetDesc();
      const native::Size physical{swap.Width,swap.Height};
      if(!owner || !owner->Accepts(*frame,physical)) {
        if(owner && !owner->checked_rebuild) {
          ReportRejected(frame->sequence,"existing target owner rejected source frame");
          return;
        }
        const auto contract=OriginalTargetContract(*frame,physical,resources->pack);
        if(!contract) {
          ReportRejected(frame->sequence,"original target contract incomplete");
          return;
        }
        owner=std::make_unique<AutomaticEorOwner>(
            OriginalFrameGpu{nullptr,host.get()},resources,*contract);
      }
      if(!owner->backend->CanReplayOriginalXdkTimeline(*frame)) {
        ReportRejected(frame->sequence,"backend cannot replay complete original timeline");
        return;
      }
      AdvanceOriginalMeshUploads(*resources->source_models,*owner->backend);
      std::vector<std::shared_ptr<const model::UploadedOriginalModel>> models;
      staged=true;
      auto draws=owner->Prepare(*frame,models);
      std::vector<std::shared_ptr<const void>> retained(models.begin(),models.end());
      const auto result=owner->native_executor->Submit(
          owner->contract.output,draws,*frame,std::move(retained));
      staged=result==gpu::NativeFrameSubmission::IncompleteSource;
      if(result!=gpu::NativeFrameSubmission::Submitted) {
        const auto* detail=owner->native_executor->LastFailure();
        ReportRejected(frame->sequence,*detail?detail:
            (staged?"incomplete original GPU source":"native GPU submission failed"));
      } else
        last_rejection_.clear();
      if(result==gpu::NativeFrameSubmission::Submitted && !presented_) {
        presented_=true;
        REXLOG_INFO("[HULK/EOR-WIN32] submitted first original frame to swapchain Present: sequence={} draws={} pixels={}x{}",
                    frame->sequence,draws.size(),physical.width,physical.height);
      }
      if(result==gpu::NativeFrameSubmission::FailedAfterGpu)
        g_eor_gpu_failures.fetch_add(1,std::memory_order_relaxed);
    } catch(const std::exception& error) {
      ReportRejected(frame->sequence,error.what());
    } catch (...) {ReportRejected(frame->sequence,"unknown original frame preparation exception");}
    if(staged && owner && owner->backend)
      owner->backend->DiscardUnsubmittedOriginalFontDraws(frame->sequence);
  }
  bool presented_=false;
  std::string last_rejection_;
};
#endif
std::shared_ptr<const VerifiedWorkerEorOwnerCatalog> BuildAutomaticOriginalOwnerCatalogForFrame(
    native_x_renderer::Device& host,std::shared_ptr<const Frame> source) {
  if(!host.device() || !host.context() || !host.swap_chain())return {};
  OfflineShaderPack pack;
  std::filesystem::path root,candidate;
  std::shared_ptr<const VerifiedWorkerEorOwnerCatalog> previous;
  {
    std::lock_guard lock(g_mutex);
    if(!g_offline_shader_pack)return {};
    pack=*g_offline_shader_pack;
    root=g_original_game_asset_root;
    candidate=g_original_material_candidate_index;
    if(!source)source=g_frames_for_native.Peek();
    previous=g_worker_owner_catalog;
  }
  if(!source || !pack.shaders || !pack.samplers || !pack.virtual_ids)return {};
  const auto& swap=host.swap_chain()->GetDesc();
  const native::Size backbuffer{swap.Width,swap.Height};
  auto resources=g_automatic_resources.lock();
  // The same window and render extent do not authorize an older bank or
  // archive generation. Verify the actual resource owner before reuse.
  if(previous && previous->device==&host && resources &&
     resources->Matches(host.device(),pack,root,candidate) &&
     previous->source_models.get()==resources->source_models.get() &&
     previous->accepts_live_source &&
     previous->accepts_live_source(*source,backbuffer))return previous;
  const auto contract=OriginalTargetContract(*source,backbuffer,pack);
  if(!contract)return {};
  if(!resources || !resources->Matches(host.device(),pack,root,candidate)) {
    const auto paths=FindAutomaticArchives(root);
    if(!paths)return {};
    resources=std::make_shared<AutomaticEorResources>(host.device(),pack,*paths,root,candidate);
  }
  auto owned=std::make_shared<AutomaticEorOwner>(OriginalFrameGpu{&host},resources,*contract);
  if(!owned->backend->CanReplayOriginalXdkTimeline(*source))return {};
  auto catalog=std::make_shared<VerifiedWorkerEorOwnerCatalog>();
  catalog->device=&host;catalog->output=contract->output;
  catalog->direct_gpu=std::shared_ptr<gpu::DiligentEorDirectBackend>(owned,owned->backend.get());
  catalog->gpu=catalog->direct_gpu;
  // A retained stream is an alias of the shared GPU/archive generation,
  // not a separate shared_ptr that could outlive its mesh upload backend.
  catalog->source_models=std::shared_ptr<world::NativeWorldStream>(
      resources,resources->source_models.get());
  catalog->source_material_candidates=owned->candidates;
  catalog->prepare_live_original_draws=[owned](const Frame& frame,
      std::vector<std::shared_ptr<const model::UploadedOriginalModel>>& retained) {
    return owned->Prepare(frame,retained);
  };
  catalog->accepts_live_source=[owned](const Frame& frame,native::Size size) {
    try {return owned->Accepts(frame,size);}
    catch (...) {return false;}
  };
  catalog->needs_target_rebuild=[owned] {return owned->checked_rebuild;};
#ifdef _WIN32
  catalog->execute_original_frame=[owned](const Frame& frame,
      std::span<const aot::BoundIndexedDraw> draws,
      std::span<const std::shared_ptr<const model::UploadedOriginalModel>> models) {
    using Result=native_x_renderer::HulkEorPresentResult;
    if(!owned->SameWorkerHandles())return Result::kNotReady;
    std::vector<std::shared_ptr<const void>> retained;
    retained.reserve(models.size());
    for(const auto& model:models)retained.emplace_back(model);
    const auto submitted=owned->native_executor->Submit(
        owned->contract.output,draws,frame,std::move(retained));
    switch(submitted) {
      case gpu::NativeFrameSubmission::Submitted:return Result::kBackbufferReady;
      case gpu::NativeFrameSubmission::FailedAfterGpu:return Result::kFailedAfterGpu;
      default:return Result::kNotReady;
    }
  };
#else
  catalog->submit_original_gpu_frame=[owned](uint64_t frame) {
    return owned->gpu_completion && owned->gpu_completion->Submit(frame);
  };
#endif
  // The new backend was constructed against this exact source contract.
  owned->checked_sequence=source->sequence;
  owned->checked_size=backbuffer;
  owned->checked_accepts=true;
  g_automatic_resources=resources;
  return catalog;
}
std::shared_ptr<const VerifiedWorkerEorOwnerCatalog> BuildAutomaticOriginalOwnerCatalog(
    native_x_renderer::Device& host) {
  return BuildAutomaticOriginalOwnerCatalogForFrame(host,{});
}

PreparedEorMixedFrame PrepareOriginalMixedDraws(
    const Frame& exact,
    const VerifiedWorkerEorOwnerCatalog& catalog,
    const OfflineShaderPack& pack) {
  if(!exact.sequence || exact.calls.empty() || !pack.shaders ||
     !pack.effects || !pack.reflection || !pack.virtual_ids ||
     !catalog.direct_gpu || catalog.gpu.get()!=catalog.direct_gpu.get())
    throw std::invalid_argument("Missing verified mixed EoR Vulkan source owner");
  PreparedEorMixedFrame output;
  if(catalog.prepare_live_original_draws) {
    output.ordered=catalog.prepare_live_original_draws(exact,output.world);
    if(output.ordered.size()!=exact.calls.size())
      throw std::invalid_argument("Live original producer omitted a source Draw");
    return output;
  }
  std::vector<std::optional<aot::BoundIndexedDraw>> slots(exact.calls.size());
  Frame static_frame{};
  static_frame.sequence=exact.sequence;
  std::vector<size_t> static_indices;
  for(size_t i=0;i<exact.calls.size();++i) {
    const auto& call=exact.calls[i];
    const auto& parent=call.original_engine_shader_method;
    const bool original_font=parent &&
        parent->original_shader_method_guest==0x8266A0C0u;
    if(!original_font) {
      static_indices.push_back(i);
      static_frame.calls.push_back(call);
      continue;
    }
    const auto& live=call.observed_vs_matrices;
    if(!live || !call.observed_live_eor_effect_pass)
      throw std::invalid_argument("Original font shader Draw is not source-resident");
    const VerifiedWorkerEorOwnerCatalog::VerifiedFontMaterial* selected=nullptr;
    std::optional<ui::VerifiedFontVertexLayout> selected_layout;
    for(const auto& font:catalog.verified_font_materials) {
      // A startup catalogue cannot know the original game's per-session
      // XDK declaration pointer. Allow its original ABI verifier to resolve
      // the CURRENT live declaration on the worker; never infer offsets
      // from a shader class name, atlas dimensions or stale guest object.
      auto live_layout=call.kind==Kind::EndVertices ?
          ui::ResolveUsaInlineFontLayout(call) : ui::ResolveUsaBufferedFontLayout(call);
      if(!live_layout && (font.resolve_verified_live_vertex_layout ||
                          font.source_vertex_layout.original_vertex_declaration_layout_verified) &&
         call.kind!=Kind::EndVertices &&
         !(call.vertex_declaration && call.vertex_declaration->font_main_elements)) {
        live_layout=font.resolve_verified_live_vertex_layout ? font.resolve_verified_live_vertex_layout(call) :
            std::optional<ui::VerifiedFontVertexLayout>{font.source_vertex_layout};
      }
      if(!live_layout)continue;
      const auto& layout=*live_layout;
      if(!layout.original_vertex_declaration_layout_verified ||
         !layout.original_font_sampler_owner_verified ||
         layout.original_stride<12 || layout.original_stride>192 ||
         layout.original_pixel_texture_slot>=16 ||
         layout.original_font_atlas_hash!=
             font.source_vertex_layout.original_font_atlas_hash ||
         layout.original_pixel_texture_slot!=
             font.source_vertex_layout.original_pixel_texture_slot ||
         (font.source_vertex_layout.original_vertex_declaration_guest &&
          layout.original_vertex_declaration_guest!=
              font.source_vertex_layout.original_vertex_declaration_guest) ||
         live->original_vertex_declaration_guest_object!=
             layout.original_vertex_declaration_guest ||
         call.original_pixel_texture_keys[layout.original_pixel_texture_slot]!=
             layout.original_font_atlas_hash ||
         font.original_effect_hash!=
             call.observed_live_eor_effect_pass->effect_hash ||
         font.original_pass_index!=
             call.observed_live_eor_effect_pass->pass_index)
        continue;
      if(selected)
        throw std::invalid_argument("Ambiguous original font declaration/atlas owner");
      selected=&font;
      selected_layout=layout;
    }
    if(!selected || !selected_layout)
      throw std::invalid_argument("Original EFontShader has no verified material/UV source");
    slots[i]=catalog.direct_gpu->StageVerifiedOriginalFontDraw(
        call,*selected_layout,exact.sequence,
        static_cast<uint32_t>(i),selected->original_font_resource_hash,
        selected->original_surface_ordinal,*pack.virtual_ids);
  }
  if(!static_frame.calls.empty()) {
    const auto resident_lookup=catalog.resident_lookup ?
        catalog.resident_lookup :
        (catalog.source_models ?
          native::VerifiedOriginalResidentLookup{
            [stream=catalog.source_models](uint32_t hash) {
              return stream->ResidentShared(hash);
            }} : native::VerifiedOriginalResidentLookup{});
    if(!resident_lookup || catalog.verified_candidates.empty())
      throw std::invalid_argument("Original world Draw has no material catalog");
    std::map<uint32_t,std::shared_ptr<const model::UploadedOriginalModel>> owners;
    std::vector<native::VerifiedXdkResourceWitness> candidates;
    for(const auto& candidate:catalog.verified_candidates) {
      const auto& template_draw=std::visit([](const auto& link)
          ->const native::VerifiedDrawSource& {return link.native_draw;},
          candidate.original_source_submission);
      const auto& expected=candidate.original_resources;
      const bool possible=std::any_of(static_frame.calls.begin(),
          static_frame.calls.end(),[&](const RawDraw& call) {
            if(!call.observed_vs_matrices)return false;
            const auto actual=native::ResourcesAtDraw(
                *call.observed_vs_matrices);
            return actual.device==expected.device &&
                   actual.vertex_declaration==expected.vertex_declaration &&
                   actual.vertex_shader==expected.vertex_shader &&
                   actual.pixel_shader==expected.pixel_shader &&
                   actual.rt0==expected.rt0 &&
                   actual.depth==expected.depth &&
                   (!call.observed_live_eor_effect_pass ||
                    (call.observed_live_eor_effect_pass->effect_hash==
                       template_draw.original_effect_hash &&
                     call.observed_live_eor_effect_pass->pass_index==
                       template_draw.original_pass_index));
          });
      if(!possible)continue;
      if(!candidate.original_guest_resource_to_material_mapping_verified ||
         !template_draw.original_model_hash)
        throw std::invalid_argument("Unverified original world material owner");
      if(!owners.contains(template_draw.original_model_hash)) {
        auto resident=resident_lookup(template_draw.original_model_hash);
        if(!resident || resident->original_resource_hash!=
             template_draw.original_model_hash ||
           resident->original_identity.platform!=model::Platform::Xbox360)
          throw std::invalid_argument("Verified world model is no longer resident");
        owners.emplace(template_draw.original_model_hash,std::move(resident));
      }
      candidates.push_back(candidate);
    }
    std::vector<const model::UploadedOriginalModel*> borrowed;
    for(const auto& [_,v]:owners) {
      output.world.push_back(v);
      borrowed.push_back(v.get());
    }
    if(candidates.empty() || borrowed.empty())
      throw std::invalid_argument("No matching source world material candidates");
    // Streamed original GPU meshes can become resident after the source
    // Vulkan backend was created; bind their live handles before replay.
    catalog.direct_gpu->AdoptVerifiedResidentWorldMeshes(borrowed);
    auto source=native::ResolveCapturedResourceDraws(
        static_frame,candidates,borrowed);
    native::ValidateLiveOriginalShaderPasses(
        static_frame,source,*pack.effects);
    auto gpu_draws=native::BindVerifiedGeometryDraws(
        source,borrowed,*pack.effects);
    if(gpu_draws.size()!=static_indices.size())
      throw std::invalid_argument("Original world geometry count incomplete");
    for(size_t i=0;i<gpu_draws.size();++i) {
      const size_t original_index=static_indices[i];
      const auto& original=exact.calls[original_index];
      const auto& live=original.observed_vs_matrices;
      const auto& draw=gpu_draws[i];
      if(!live || !original.observed_live_vertex_shader_header ||
         !original.observed_live_pixel_shader_header ||
         !aot::MatchesOriginalLiveXdkShaderSource(
             *pack.virtual_ids,*original.observed_live_vertex_shader_header,
             live->original_vs_guest_object,aot::Stage::Vertex,
             draw.vertex_source_sha256,exact.sequence,
             static_cast<uint32_t>(original_index)) ||
         !aot::MatchesOriginalLiveXdkShaderSource(
             *pack.virtual_ids,*original.observed_live_pixel_shader_header,
             live->original_ps_guest_object,aot::Stage::Pixel,
             draw.pixel_source_sha256,exact.sequence,
             static_cast<uint32_t>(original_index)))
        throw std::invalid_argument("Original world VS/PS source header changed");
      auto source_bound=std::move(gpu_draws[i]);
      source_bound.draw.source_ordinal=static_cast<uint32_t>(original_index);
      slots[original_index]=std::move(source_bound);
    }
  }
  output.ordered.reserve(slots.size());
  for(auto& item:slots) {
    if(!item)
      throw std::invalid_argument("Original Vulkan scene has an unowned source Draw");
    output.ordered.push_back(std::move(*item));
  }
  return output;
}
native_x_renderer::HulkEorPresentResult ReplayVerifiedMixedOriginalFrame(
    native_x_renderer::Device& host,
    std::shared_ptr<const VerifiedWorkerEorOwnerCatalog> owner,
    std::shared_ptr<const Frame> snapshot,uint64_t source_sequence,
    bool independent_claimed=false) noexcept {
  using Result=native_x_renderer::HulkEorPresentResult;
  if(!snapshot || !owner || !owner->direct_gpu || !host.context() ||
     !host.swap_chain() || snapshot->sequence!=source_sequence ||
     owner->device!=&host ||
     !ValidateOriginalCommandTimeline(*snapshot))
    return Result::kNotReady;
  const auto& swap=host.swap_chain()->GetDesc();
  if(swap.Width!=owner->output.backbuffer.width ||
     swap.Height!=owner->output.backbuffer.height)
    return Result::kNotReady;
  OfflineShaderPack pack;
  {
    std::lock_guard lock(g_mutex);
    if(g_worker_owner_catalog!=owner || !g_offline_shader_pack)
      return Result::kNotReady;
    // Reserve this exact complete source Present BEFORE material/PSO staging.
    // Previously a new guest Present during expensive preparation invalidated
    // all work, causing indefinite fallback in an otherwise valid scene.
    if(!independent_claimed) {
      auto claimed=g_frames_for_native.ClaimCleanIfSequence(source_sequence);
      if(!claimed || claimed!=snapshot)return Result::kNotReady;
      snapshot=std::move(claimed);
    } else if(!owner->prepare_live_original_draws || !owner->accepts_live_source) {
      return Result::kNotReady;
    }
    pack=*g_offline_shader_pack;
  }
  // This path does not invoke ResolveCapturedResourceDraws for EFontShader:
  // a transient vertex buffer is NOT a static MODELS.ARC mesh. The exact
  // ordered sequence is reconstructed with mixed source-proven producers.
  // Guard every rejection AFTER staging: an overwritten source Present
  // must not strand one generation of glyph VB/IB allocations indefinitely.
  struct DiscardUnsubmitted {
    gpu::DiligentEorDirectBackend* backend;
    uint64_t frame;
    bool active=true;
    ~DiscardUnsubmitted() {
      if(active && backend)
        backend->DiscardUnsubmittedOriginalFontDraws(frame);
    }
  } staged{owner->direct_gpu.get(),source_sequence};
  bool began_gpu=false;
  try {
    if(!owner->gpu->CanReplayOriginalXdkTimeline(*snapshot))
      return Result::kNotReady;
    auto prepared=PrepareOriginalMixedDraws(*snapshot,*owner,pack);
    if(prepared.ordered.size()!=snapshot->calls.size())
      return Result::kNotReady;
    for(const auto& draw:prepared.ordered)
      if(!pack.shaders->Find(draw.vertex_source_sha256,aot::Stage::Vertex) ||
         (!draw.draw.original_vertex_only && !pack.shaders->Find(draw.pixel_source_sha256,aot::Stage::Pixel)))
        return Result::kNotReady;
    // The claimed source frame is immutable, including all Draw constants
    // and buffer bytes. A newer guest Present belongs to the next worker
    // invocation; it cannot replace any resource in this frame.
    {
      std::lock_guard lock(g_mutex);
      if(g_worker_owner_catalog!=owner || !g_offline_shader_pack ||
         g_offline_shader_pack->shaders!=pack.shaders ||
         g_offline_shader_pack->effects!=pack.effects ||
         g_offline_shader_pack->reflection!=pack.reflection ||
         g_offline_shader_pack->samplers!=pack.samplers ||
         g_offline_shader_pack->virtual_ids!=pack.virtual_ids)
        return Result::kNotReady;
    }
    const auto& current=host.swap_chain()->GetDesc();
    if(current.Width!=owner->output.backbuffer.width ||
       current.Height!=owner->output.backbuffer.height)return Result::kNotReady;
    if(owner->execute_original_frame) {
      const auto result=owner->execute_original_frame(
          *snapshot,prepared.ordered,prepared.world);
      if(result==Result::kBackbufferReady) {
        staged.active=false;
        g_eor_ready_backbuffers.fetch_add(1,std::memory_order_relaxed);
      } else if(result==Result::kFailedAfterGpu) {
        staged.active=false;
        g_eor_gpu_failures.fetch_add(1,std::memory_order_relaxed);
      }
      return result;
    }
    // External catalogue implementations keep their original replay path.
    aot::ReplayAotScene(owner->output,prepared.ordered,
        *pack.shaders,*pack.effects,*pack.reflection,*owner->gpu,
        snapshot.get(),&began_gpu);
    // GPU owns every source-authenticated Draw until the real Diligent
    // fence, not just its CPU Present ordinal. Normal frames enqueue a
    // signal and return immediately; backlog waits for the oldest fence.
    // The old synchronous completion path remains for external catalogue
    // implementations that cannot provide an explicit completion fence.
    staged.active=false;
    if(owner->submit_original_gpu_frame) {
      if(!owner->submit_original_gpu_frame(source_sequence))
        throw std::runtime_error("Original EoR Vulkan frame fence submission failed");
    } else {
      host.context()->Flush();
      host.context()->WaitForIdle();
      owner->direct_gpu->RetireCompletedOriginalFontDraws(source_sequence);
    }
    g_eor_ready_backbuffers.fetch_add(1,std::memory_order_relaxed);
    return Result::kBackbufferReady;
  } catch (...) {
    // Surface the original shader/material/Draw rejection from the mixed
    // world+Font_Main path. Keep it throttled on a high-FPS guest, but do
    // not silently eat the only available cause of a missing first frame.
    if(source_sequence<=5 || source_sequence%120==0) {
      try { throw; }
      catch(const std::exception& error) {
        REXLOG_WARN("[HULK/EOR-MIXED] source frame={} Vulkan replay failed "
                    "(began_gpu={}): {}",source_sequence,began_gpu,error.what());
      } catch (...) {
        REXLOG_WARN("[HULK/EOR-MIXED] source frame={} Vulkan replay failed "
                    "(began_gpu={}, unknown exception)",source_sequence,began_gpu);
      }
    }
    if(began_gpu) {
      staged.active=false;
      try {
        host.context()->Flush();
        host.context()->WaitForIdle();
        owner->direct_gpu->RetireCompletedOriginalFontDraws(source_sequence);
      } catch (...) {
        // Device loss: keep references for backend shutdown, never
        // free in-flight allocations under an unverified GPU fence.
      }
      g_eor_gpu_failures.fetch_add(1,std::memory_order_relaxed);
      return Result::kFailedAfterGpu;
    }
    return Result::kNotReady;
  }
}
// Persist ONLY independently observed live original ShaderEffects pairs as
// private AOT compilation demands. This works even before any GPU source
// owner catalogue, so the first real city draw can feed the original
// XenosRecomp/DXC build without scanning/compiling all 5194 programs.
void RecordObservedOriginalAotPairDemand(const Frame& snapshot) noexcept {
  if(!snapshot.sequence || snapshot.dropped || snapshot.dropped_commands)return;
  try {
  std::shared_ptr<const aot::OriginalVirtualShaderIdentity> identities;
  std::shared_ptr<const aot::EffectCatalog> catalog;
  std::shared_ptr<const aot::ShaderBank> shaders;
  {
    std::lock_guard lock(g_mutex);
    // A complete authenticated bank needs no more disk-backed AOT hints.
    if(g_offline_shader_pack && g_offline_shader_pack->shaders &&
       g_offline_shader_pack->shaders->expected() &&
       g_offline_shader_pack->shaders->size()==g_offline_shader_pack->shaders->expected())
      return;
    identities=g_original_shader_capture_index;
    catalog=g_original_effect_capture_catalog;
    if(g_offline_shader_pack)shaders=g_offline_shader_pack->shaders;
  }
  std::set<std::pair<uint32_t,uint32_t>> observed;
  for(size_t ordinal=0;ordinal<snapshot.calls.size();++ordinal) {
    const auto& draw=snapshot.calls[ordinal];
    if(draw.original_pixel_stage==OriginalPixelStageKind::VertexOnlyDepthStencil &&
       !draw.observed_live_pixel_shader_header && !draw.observed_live_eor_effect_pass &&
       draw.observed_live_vertex_shader_header && identities && catalog && ordinal<=UINT32_MAX) {
      const auto& witness=*draw.observed_live_vertex_shader_header;
      if(!witness.alias_paired_vertex_source) {
        const auto vs=aot::ResolveOriginalLiveXdkShaderSource(*identities,witness,
            witness.original_xdk_shader_guest_object,aot::Stage::Vertex,
            snapshot.sequence,static_cast<uint32_t>(ordinal));
        if(vs&&(!shaders||!shaders->Find(*vs,aot::Stage::Vertex)))
          if(const auto hint=catalog->CompilationPassForVertex(*vs))
            observed.emplace(hint->effect_hash,hint->pass_index);
      }
      continue; // compilation hint only; no invented original PS or pass
    }
    if(!draw.observed_live_eor_effect_pass ||
       !draw.observed_live_vertex_shader_header ||
       !draw.observed_live_pixel_shader_header)continue;
    // The identity-only fast path authenticated both live bound shader
    // headers and their effect at this exact Draw ordinal. Its AOT request
    // must survive even without a full matrix/geometry snapshot.
    const auto& effect=*draw.observed_live_eor_effect_pass;
    if(effect.effect_hash && effect.pass_index!=UINT32_MAX)
      observed.emplace(effect.effect_hash,effect.pass_index);
  }
  if(observed.empty())return;
    std::lock_guard lock(g_mutex);
    if(g_original_shader_aot_demand_path.empty() ||
       identities!=g_original_shader_capture_index || catalog!=g_original_effect_capture_catalog)return;
    std::vector<std::pair<uint32_t,uint32_t>> unseen;
    for(const auto& item:observed)
      if(!g_original_shader_aot_requests.contains(item))
        unseen.push_back(item);
    if(unseen.empty())return;
    std::error_code error;
    const auto parent=g_original_shader_aot_demand_path.parent_path();
    std::filesystem::create_directories(parent,error);
    if(error)return;
    std::ofstream private_file(g_original_shader_aot_demand_path,
                               std::ios::out|std::ios::app);
    if(!private_file)return;
    for(const auto& [effect,pass]:unseen)
      private_file<<effect<<":"<<pass<<"\n";
    private_file.flush();
    if(!private_file)return;
    for(const auto& item:unseen)
      g_original_shader_aot_requests.insert(item);
  } catch (...) {
    // Missing disk access must never prevent PPC game logic or Vulkan
    // fallback. The next real source frame can retry this compilation hint.
  }
}
// The Windows AOT compiler atomically replaces hulk_shader_bank.bin while
// the original game continues producing real XDK Draws. Adopt only validated
// new original source VS/PS modules, without restarting the game or touching
// Vulkan GPU pipeline objects. An active GPU owner still requires an
// explicit completion fence before changing its shader generation.
bool AdoptNewPrivateOriginalSpirvIfSafe(
    uint64_t sequence,Diligent::IDeviceContext* context) noexcept {
  if(!sequence)return false;
  try {
    std::filesystem::path root;
    {
      std::lock_guard lock(g_mutex);
      // A source owner may already exist while only the FIRST VS/PS
      // pair was compiled. Requiring an empty owner here made later
      // shader growth permanently impossible without restarting Hulk.
      // Such a generation may change ONLY if its source factory can
      // rebuild every real Vulkan PSO after the GPU completion fence.
      if(g_original_shader_aot_demand_path.empty() ||
         (g_worker_owner_catalog && !g_worker_catalog_factory))
        return false;
      // Cumulative AOT cannot add any further programs after the complete
      // authenticated bank has been installed. Reopening the same bank
      // at the first Present discards valid owners and forces a second
      // source/metadata load for no benefit. Replacing a complete bank
      // now requires a restart (or explicit InstallVerifiedSourceShaderPack).
      if(g_offline_shader_pack && g_offline_shader_pack->shaders &&
         g_offline_shader_pack->shaders->HasFullArchiveCoverage() &&
         g_offline_shader_pack->effects && g_offline_shader_pack->reflection &&
         g_offline_shader_pack->samplers && g_offline_shader_pack->virtual_ids)
        return false;
      // The mailbox coalesces frames. Poll by elapsed source epochs even
      // when the consumer never receives an exact multiple of 120.
      if(sequence>5 && g_last_incremental_spirv_poll &&
         sequence>=g_last_incremental_spirv_poll &&
         sequence-g_last_incremental_spirv_poll<120)return false;
      g_last_incremental_spirv_poll=sequence;
      root=g_original_shader_aot_demand_path.parent_path();
    }
    const auto compiled=root/"hulk_shader_bank.bin";
    const auto alias=root/"HEORSPV1.bin";
    const auto file=std::filesystem::is_regular_file(compiled)?compiled:alias;
    std::error_code ec;
    if(!std::filesystem::is_regular_file(file,ec) || ec)return false;
    const auto size=std::filesystem::file_size(file,ec);
    if(ec || size<16 || size>1024ull*1024ull*1024ull)return false;
    const auto stamp=std::filesystem::last_write_time(file,ec);
    if(ec)return false;
    bool changed=false;
    {
      std::lock_guard lock(g_mutex);
      if(g_worker_owner_catalog && !g_worker_catalog_factory)
        return false;
      changed=file!=g_last_incremental_spirv_file ||
              size!=g_last_incremental_spirv_size ||
              stamp!=g_last_incremental_spirv_write;
    }
    if(!changed)return false;
    // The prior owner can retain live Vulkan PSOs, textures and scene
    // attachments. Source generation changes may destroy those owners
    // ONLY after the SAME host GPU worker has finished its pending draws.
    // Never install a new bank across an in-flight source frame.
    bool has_old_gpu_owner=false;
    {
      std::lock_guard lock(g_mutex);
      has_old_gpu_owner=static_cast<bool>(g_worker_owner_catalog) ||
                         static_cast<bool>(g_verified_worker_frame);
    }
    if(has_old_gpu_owner) {
      if(!context)return false;
      context->Flush();
      context->WaitForIdle();
    }
    // AOT writer installs HEORSPV1 first, then the four authenticated
    // metadata banks. Do NOT mark a partially installed generation as
    // already examined: the next guest frame retries the same source.
    if(InstallVerifiedSourceShaderPackFromDirectory(root)) {
      {
        std::lock_guard lock(g_mutex);
        g_last_incremental_spirv_file=file;
        g_last_incremental_spirv_size=size;
        g_last_incremental_spirv_write=stamp;
      }
      REXLOG_INFO("[HULK/EOR-AOT] admitted newly compiled original "
                  "VS/PS bank; GPU owner PSOs must rebuild on next Present");
      // The caller's locally cached owner/ready pointers belong to the
      // previous shader generation. Do not submit even ONE more stale
      // original Vulkan Draw on this epoch after the bank changed.
      return true;
    }
  } catch(...) {
    // File replacement in progress or source directory unavailable:
    // guest original Present must not be disturbed.
  }
  return false;
}
} // namespace

#ifdef _WIN32
bool StartExclusiveEorFrames(std::unique_ptr<gpu::EorWin32VulkanHost> host,
    std::function<bool(std::function<void()>)> post) noexcept {
  if(!host || !host->Device() || !host->Context() || !host->SwapChain() || !post)return false;
  try {
    std::optional<OfflineShaderPack> pack;
    std::filesystem::path root,candidate;
    {
      std::lock_guard lock(g_mutex);
      if(g_notify_exclusive_frame || !g_original_shader_capture_index ||
         !g_original_effect_capture_catalog)return false;
      pack=g_offline_shader_pack;
      root=g_original_game_asset_root;
      candidate=g_original_material_candidate_index;
    }
    const auto paths=FindAutomaticArchives(root);
    if(!paths)return false;
    auto session=std::make_shared<ExclusiveEorSession>();
    session->host=std::move(host);
    session->root=root;
    session->candidate=candidate;
    session->archives=*paths;
    if(pack && pack->samplers && pack->virtual_ids)
      session->resources=std::make_shared<AutomaticEorResources>(
          session->host->Device(),*pack,*paths,root,candidate);
    session->post=std::move(post);
    std::lock_guard lock(g_mutex);
    if(g_notify_exclusive_frame)return false;
    g_notify_exclusive_frame=[session] {session->NotifyLocked();};
    return true;
  } catch (...) {return false;}
}
void StopExclusiveEorFrames() noexcept {
  // The window thread also owns Drain(). Unregister before disposing any
  // fences or the swapchain. Already posted callbacks hold only weak refs.
  std::function<void()> retired;
  {
    std::lock_guard lock(g_mutex);
    retired.swap(g_notify_exclusive_frame);
  }
}
#endif
native_x_renderer::HulkEorPresentResult TryWorkerEorFrame(
    native_x_renderer::Device& host) noexcept {
  using Result=native_x_renderer::HulkEorPresentResult;
  if(!CaptureEnabled() || !REXCVAR_GET(hulk_eor_vulkan_replay))
    return Result::kNotReady;
  std::shared_ptr<const VerifiedWorkerEorFrame> ready;
  std::shared_ptr<const VerifiedWorkerEorOwnerCatalog> owner_catalog;
  std::shared_ptr<const Frame> source_snapshot;
  uint64_t source_sequence=0;
  bool has_source_catalog_factory=false;
  bool built_in_source=false,independent_claimed=false;
  {
    std::lock_guard lock(g_mutex);
    ready=g_verified_worker_frame;
    owner_catalog=g_worker_owner_catalog;
    // The shipped game installs its source root and shader pack at startup.
    // Previously the only producer entry was an externally registered
    // callback, but nothing registered one in the actual game.
    if(!g_worker_catalog_factory && !g_worker_owner_catalog &&
       g_offline_shader_pack && !g_original_game_asset_root.empty()) {
      g_worker_catalog_factory=std::make_shared<const VerifiedWorkerEorCatalogFactory>(
          &BuildAutomaticOriginalOwnerCatalog);
      g_worker_catalog_factory_last_attempt=0;
    }
    has_source_catalog_factory=static_cast<bool>(g_worker_catalog_factory);
    if(g_worker_catalog_factory) {
      const auto* producer=g_worker_catalog_factory->target<
          decltype(&BuildAutomaticOriginalOwnerCatalog)>();
      built_in_source=producer && *producer==&BuildAutomaticOriginalOwnerCatalog;
    }
    if(ready && ready->owner_catalog &&
       ready->owner_catalog!=owner_catalog)
      ready.reset(); // last source catalogue was replaced or cleared
    source_snapshot=g_frames_for_native.Peek();
    if(!source_snapshot)return Result::kNotReady;
    source_sequence=source_snapshot->sequence;
    if(built_in_source && !source_snapshot->dropped &&
       !source_snapshot->dropped_commands) {
      // The built-in target graph starts from original per-frame clears
      // and independently owns every native attachment and Resolve. Hold
      // the latest immutable frame BEFORE archive work, target planning,
      // prefetch and PSO creation. A faster PPC producer must not starve it.
      auto claimed=g_frames_for_native.ClaimLatestIndependentIfSequence(source_sequence);
      if(!claimed || claimed!=source_snapshot)return Result::kNotReady;
      source_snapshot=std::move(claimed);
      independent_claimed=true;
    }
    if(!independent_claimed && g_frames_for_native.MissedSinceTake()) {
      // A dropped mailbox epoch is never permitted to touch Vulkan.
      // Discard it atomically while still under the original Present lock,
      // release the temporary preview first, and let the NEXT frame compete.
      source_snapshot.reset();
      if(g_frames_for_native.DiscardIfSequence(source_sequence))
        g_verified_worker_frame.reset(); // require a fresh source-bound bundle
      return Result::kNotReady;
    }
  }
  if(AdoptNewPrivateOriginalSpirvIfSafe(source_sequence,host.context()))
    return Result::kNotReady; // next source epoch rebuilds matching PSOs
  // The original live ShaderEffects pair is already authenticated by
  // BOTH guest VS/PS XDK ShaderContainer headers and HEORMAT2. Queue its
  // exact VS+PS for private offline AOT even if the full material owner is
  // not yet assembled and the current frame uses the renderer fallback.
  RecordObservedOriginalAotPairDemand(*source_snapshot);
  // Full-frame replay cannot succeed when any Draw has an unresolved
  // program. The authenticated identity/AOT demand above remains intact.
  // Do not build target graphs, upload models or prepare GPU state for a
  // frame already known to require the existing renderer.
  if(built_in_source &&
     (source_snapshot->calls.empty() || source_snapshot->dropped ||
      source_snapshot->dropped_commands ||
      !std::all_of(source_snapshot->calls.begin(),source_snapshot->calls.end(),
          [](const RawDraw& draw) {
            return HasRequiredReplaySnapshot(draw);
          }) ||
      !ValidateOriginalCommandTimeline(*source_snapshot)))
    return Result::kNotReady;
  // A valid captured Draw alone is not a complete GPU catalogue.
  // Without a registered source-authenticated material/mesh factory or an
  // already-installed owner, the worker cannot possibly reach Vulkan Present.
  // Report this concrete upstream blocker instead of silently repeating
  // a kNotReady fallback forever. Never invent owners from candidates.
  if(!owner_catalog && !has_source_catalog_factory) {
    if(source_sequence<=5 || source_sequence%120==0)
      REXLOG_WARN("[HULK/EOR-PRESENT] source frame={} has no verified "
                  "material/mesh owner catalog or source factory; "
                  "independent Vulkan replay cannot begin",source_sequence);
    return Result::kNotReady;
  }
  // Material registration is NOT allowed to invent EoR material/mesh
  // ownership from shader names, an index archive candidate, or a PM4
  // pointer. Ask the owner's *source-authenticated* asset loader on the
  // Vulkan worker, using this real device and current completed Present.
  // No source factory => existing renderer remains sole presenter.
  // Recheck generation under lock and never hold the original XDK capture
  // mutex while loading, uploading or compiling any GPU resources.
  bool target_transition=false;
  if(built_in_source && owner_catalog && owner_catalog->device==&host &&
     owner_catalog->accepts_live_source && owner_catalog->needs_target_rebuild &&
     host.swap_chain()) {
    const auto& swap=host.swap_chain()->GetDesc();
    try {
      if(!owner_catalog->accepts_live_source(*source_snapshot,{swap.Width,swap.Height}))
        target_transition=owner_catalog->needs_target_rebuild();
    } catch (...) {}
  }
  std::shared_ptr<const VerifiedWorkerEorCatalogFactory> factory;
  std::optional<OfflineShaderPack> factory_shader_generation;
  {
    std::lock_guard lock(g_mutex);
    // A renderer can stream more original EoR models/materials after the
    // first city/intro frame. Refresh a genuine source catalogue at most
    // once per 60 completed Present epochs in a stable scene. A verified
    // RT-graph change (pause menu, loading, native resize) needs prompt
    // rebuilding, while failed allocations are still bounded to one
    // attempt per four source epochs. A same pointer preserves its PSOs.
    if(g_worker_catalog_factory && g_offline_shader_pack &&
       g_offline_shader_pack->virtual_ids &&
       (!g_worker_catalog_factory_last_attempt ||
        (source_sequence>g_worker_catalog_factory_last_attempt &&
         source_sequence-g_worker_catalog_factory_last_attempt>=
             (target_transition?4u:60u)))) {
      factory=g_worker_catalog_factory;
      factory_shader_generation=*g_offline_shader_pack;
      g_worker_catalog_factory_last_attempt=source_sequence;
    }
  }
  if(factory) {
    try {
      auto verified=built_in_source ?
          BuildAutomaticOriginalOwnerCatalogForFrame(host,source_snapshot) :
          (*factory)(host);
      if(verified && verified->device==&host) {
        // A source Present may advance during GPU asset loading; keep
        // stale-frame detection separate from the atomically checked
        // material factory generation and shader-bank owner.
        {
          std::lock_guard lock(g_mutex);
          // The built-in archive owner is persistent, not a draw witness.
          // Keep it if loading spans a Present; actual replay still requires
          // a fresh source contract and an exact mailbox epoch.
          if(!verified->prepare_live_original_draws &&
             (g_frames_for_native.MissedSinceTake() ||
              !g_frames_for_native.Peek() ||
              g_frames_for_native.Peek()->sequence!=source_sequence))
            verified.reset();
        }
        // If the factory already authenticated the original GPU material
        // and shader/pass ownership, source original Xbox models can now be
        // loaded automatically from the exact user's game installation.
        // The archive reader validates the pinned USA index SHA and models
        // BEFORE starting async I/O. Only model hashes carried by the
        // source-verified owner witnesses will be queued for GPU upload.
        if(verified && !verified->source_models && verified->direct_gpu) {
          // Retain authenticated streamed model buffers when the factory
          // refreshes the source material catalogue on the SAME GPU device.
          if(owner_catalog && owner_catalog->source_models &&
             owner_catalog->direct_gpu==verified->direct_gpu &&
             owner_catalog->source_models->UsesGpuBackend(
                 verified->direct_gpu->OriginalModelUploader())) {
            auto completed=std::make_shared<VerifiedWorkerEorOwnerCatalog>(
                *verified);
            completed->source_models=owner_catalog->source_models;
            verified=std::move(completed);
          }
        }
        if(verified && !verified->source_models && verified->direct_gpu) {
          std::filesystem::path game_root;
          {
            std::lock_guard lock(g_mutex);
            game_root=g_original_game_asset_root;
          }
          if(!game_root.empty()) {
            const auto base=game_root/"DATA"/"XENON";
            using SourcePath=std::pair<std::filesystem::path,std::filesystem::path>;
            const std::array<SourcePath,8> sources{{
                {base/"INDEX.IND",base/"MODELS.ARC"},
                {base/"indexXBOX360.ind",base/"MODELS.ARC"},
                {base/"USENGLISH"/"INDEX.IND",base/"MODELS.ARC"},
                {base/"UKENGLISH"/"INDEX.IND",base/"MODELS.ARC"},
                // The owner's extracted original source archive may live
                // beside the original game's default.xex. Both accepted
                // USA model indices are individually SHA-256 pinned.
                {game_root/"indexXBOX360.ind",game_root/"MODELS.ARC"},
                {game_root/"INDEX.IND",game_root/"MODELS.ARC"},
                {game_root/"index(1).ind",game_root/"MODELS.ARC"},
                {game_root/"Data"/"Xenon"/"INDEX.IND",
                 game_root/"Data"/"Xenon"/"MODELS.ARC"},
            }};
            for(const auto& [index,models]:sources) {
              if(!std::filesystem::is_regular_file(index) ||
                 !std::filesystem::is_regular_file(models))
                continue;
              try {
                auto source=std::make_unique<world::ArchiveModelSource>(
                    index,models);
                auto world_stream=std::make_shared<world::NativeWorldStream>(
                    std::move(source),
                    verified->direct_gpu->OriginalModelUploader(),
                    std::vector<world::LodTier>{});
                auto completed=std::make_shared<VerifiedWorkerEorOwnerCatalog>(
                    *verified);
                completed->source_models=std::move(world_stream);
                // The offline HEORCND1 binary is only a fast cache for the
                // already-authenticated original Xbox material census.
                // Directly generate its SAME genuine model/surface/texture
                // lookup from the user's pinned INDEX.IND + MODELS.ARC if
                // the private sidecar is absent, stale or corrupt. No
                // hand-written material/texture/PSO associations are used.
                if(!completed->source_material_candidates) {
                  auto candidates=
                      std::make_shared<world::OriginalMaterialCandidateIndex>();
                  std::filesystem::path private_cached_index;
                  {
                    std::lock_guard lock(g_mutex);
                    private_cached_index=g_original_material_candidate_index;
                  }
                  bool preverified_cache=false;
                  if(!private_cached_index.empty() &&
                     std::filesystem::is_regular_file(private_cached_index)) {
                    try {
                      candidates->Load(private_cached_index);
                      constexpr std::array<uint8_t,32> kSourceModelIndex{
                          0x46u,0xe6u,0xc6u,0xecu,0x9fu,0x74u,0x28u,0xbau,
                          0x3au,0x17u,0x37u,0xabu,0x7eu,0xbau,0xd5u,0x64u,
                          0xc1u,0xb0u,0x27u,0xb0u,0x06u,0xc3u,0xe3u,0x51u,
                          0x56u,0xd2u,0xe0u,0x36u,0xabu,0x8bu,0x28u,0x20u};
                      preverified_cache=
                          candidates->OriginalIndexDigest()==kSourceModelIndex &&
                          candidates->LinkCount()==41745;
                    } catch(...) {
                      preverified_cache=false;
                    }
                  }
                  if(!preverified_cache)
                    candidates->LoadFromOriginalXboxArchive(index,models);
                  completed->source_material_candidates=std::move(candidates);
                }
                verified=std::move(completed);
                break;
              } catch(const std::exception&) {
                // A region-mismatched index or missing physical game model
                // archive is not a valid native EoR resource source.
              }
            }
          }
        }
        // Preload a bounded reverse lookup of authentic MODELS.ARC
        // surfaces and indexed Textures.ARC hashes. The owner's source
        // index uses an independently verified USA INDEX layout: Models
        // (4174) and Textures (6669) resource hashes, offsets and sizes
        // match the independently SHA-pinned USA index used by
        // ArchiveModelSource. It is a prefetch HINT, not a Draw witness.
        if(verified && verified->source_models &&
           !verified->source_material_candidates) {
          if(owner_catalog && owner_catalog->source_material_candidates &&
             owner_catalog->source_models==verified->source_models &&
             owner_catalog->direct_gpu==verified->direct_gpu) {
            auto completed=std::make_shared<VerifiedWorkerEorOwnerCatalog>(*verified);
            completed->source_material_candidates=owner_catalog->source_material_candidates;
            verified=std::move(completed);
          } else {
            std::filesystem::path candidate_path;
            {
              std::lock_guard lock(g_mutex);
              candidate_path=g_original_material_candidate_index;
            }
            if(!candidate_path.empty() &&
               std::filesystem::is_regular_file(candidate_path)) {
              try {
                auto index=std::make_shared<world::OriginalMaterialCandidateIndex>();
                index->Load(candidate_path);
                // This source-only catalog was built from the supplied,
                // original complete Xbox USA 4174-model material census.
                // Never accept another region or arbitrary hash mapping.
                constexpr std::array<uint8_t,32> kPrivateSourceIndex{
                    0x46u,0xe6u,0xc6u,0xecu,0x9fu,0x74u,0x28u,0xbau,0x3au,0x17u,0x37u,0xabu,0x7eu,0xbau,0xd5u,0x64u,0xc1u,0xb0u,0x27u,0xb0u,0x06u,0xc3u,0xe3u,0x51u,0x56u,0xd2u,0xe0u,0x36u,0xabu,0x8bu,0x28u,0x20u};
                if(index->OriginalIndexDigest()!=kPrivateSourceIndex)
                  throw std::invalid_argument("Wrong original USA material candidate index");
                auto completed=std::make_shared<VerifiedWorkerEorOwnerCatalog>(*verified);
                completed->source_material_candidates=std::move(index);
                verified=std::move(completed);
              } catch(const std::exception&) {
                // A mismatched/corrupt private lookup never authorizes
                // shader creation or Vulkan Draw and cannot poison fallback.
              }
            }
          }
        }
        // A source loader may return the same immutable catalogue on
        // successive 60-frame polls: do not discard expensive Vulkan PSOs
        // unless real owner/LOD/texture residency changed. Otherwise
        // publish only if the previously observed catalogue generation
        // has not been replaced while our factory compiled resources.
        if(verified && verified!=owner_catalog) {
          // World shaders/texture SRVs and their Diligent PSOs can still
          // be referenced by the prior async Vulkan frame. The factory's
          // updated private asset catalogue is NOT a Vulkan completion
          // fence. Flush and wait on this exact original shared worker
          // device before allowing replacement to release any old
          // shader/mesh/texture generations. If the device is lost,
          // retain the old source owner rather than risk a use-after-free.
          if(owner_catalog) {
            if(!host.context())return Result::kNotReady;
            host.context()->Flush();
            host.context()->WaitForIdle();
          }
        }
        if(verified && verified!=owner_catalog &&
           InstallVerifiedWorkerEorOwnerCatalogInternal(
               std::move(verified),&factory,
               &*factory_shader_generation,&owner_catalog)) {
          std::lock_guard lock(g_mutex);
          if(g_worker_catalog_factory==factory &&
             g_offline_shader_pack &&
             g_offline_shader_pack->shaders==factory_shader_generation->shaders &&
             g_offline_shader_pack->effects==factory_shader_generation->effects &&
             g_offline_shader_pack->reflection==factory_shader_generation->reflection &&
             g_offline_shader_pack->samplers==factory_shader_generation->samplers &&
             g_offline_shader_pack->virtual_ids==factory_shader_generation->virtual_ids)
            owner_catalog=g_worker_owner_catalog;
        }
      }
    } catch (...) {
      // A failed original archive/material resolver cannot bring down
      // the PPC guest or submit a partial world-only Vulkan image.
    }
  }
  // Source-derived texture hashes independently identify potential
  // archived mesh/material SURFACES even before a complete GPU witness
  // exists. Request only a narrowly bounded candidate set per live
  // original EoR Draw; all subsequent guest VB/IB/effect/PSO validation
  // remains unchanged. This unblocks on-demand city-model I/O instead of
  // requiring a manually installed list of thousands of model hashes.
  if(owner_catalog && owner_catalog->source_models &&
     owner_catalog->source_material_candidates &&
     owner_catalog->direct_gpu &&
     owner_catalog->source_models->UsesGpuBackend(
         owner_catalog->direct_gpu->OriginalModelUploader())) {
    try {
      std::set<uint32_t> candidate_hashes;
      using PrefetchKey=std::pair<uint16_t,std::array<uint32_t,16>>;
      std::set<PrefetchKey> seen_texture_sets;
      for(const auto& call:source_snapshot->calls) {
        const auto& shader=call.original_engine_shader_method;
        if(!shader || !shader->original_shader_method_vtable_verified ||
           !shader->original_pass_vs_ps_xdk_binding_verified ||
           !shader->original_pass_sampler_slots_verified ||
           !shader->original_pass_live_texture_objects_present ||
           !call.observed_live_eor_effect_pass ||
           !call.observed_live_vertex_shader_header ||
           !call.observed_live_pixel_shader_header ||
           !call.observed_vs_matrices ||
           !shader->original_pass_sampler_slot_mask)
          continue;
        // Unique material *ownership* still requires full source evidence,
        // but an ambiguous shared city texture is permitted to prefetch
        // a small, rotating batch of REAL original model archive records.
        // Do not starve first-frame GPU mesh residency when >12 models
        // share exactly the same texture(s).
        auto mask=shader->original_pass_sampler_slot_mask;
        auto keys=call.original_pixel_texture_keys;
        // The original archive lookup is immutable; repeated texture sets
        // on adjacent depth/color/material passes must not repeat the same
        // rotating page search. Keep all original page and owner limits.
        for(uint32_t slot=0;slot<16;++slot)
          if(!(mask&(1u<<slot)))keys[slot]=0;
        if(!seen_texture_sets.emplace(mask,keys).second)continue;
        const auto candidates=
            owner_catalog->source_material_candidates->
                PrefetchLiveSurfaceCandidatesPage(
                    keys,mask,static_cast<size_t>(source_sequence/60u),12);
        for(const auto& candidate:candidates) {
          if(candidate_hashes.size()>=96)break;
          candidate_hashes.insert(candidate.original_model_hash);
        }
        if(candidate_hashes.size()>=96)break;
      }
      if(candidate_hashes.size()<=96 && !candidate_hashes.empty()) {
        const std::vector<uint32_t> requests(
            candidate_hashes.begin(),candidate_hashes.end());
        owner_catalog->source_models->RequestSourceVerifiedModels(requests);
      }
    } catch (...) {
      // An optional original archive texture prefetch hint cannot
      // authorize or reject a genuine shader/material GPU Draw.
    }
  }
  // Request authentic world-model GPU residency for ONLY those source
  // catalog owners whose original XDK device/shader/declaration/target
  // bindings agree with this Present. The request is asynchronous: an
  // unavailable model never authorizes a partial Vulkan image, but can
  // become resident before a later exact Draw. Do NOT request every one
  // of the original 4174 world models on each frame.
  if(owner_catalog &&
     (owner_catalog->request_source_models || owner_catalog->source_models) &&
     !owner_catalog->verified_candidates.empty()) {
    try {
      // One indexed lookup per source Draw instead of O(catalog * Draws)
      // repeated scanning during every original 60/165-FPS Present.
      // Without an exact live effect/pass, shader objects alone are not
      // sufficient to select resident model candidates.
      using SourcePassKey=std::array<uint32_t,8>;
      std::set<SourcePassKey> active_source_passes;
      for(const auto& call:source_snapshot->calls) {
        if(!call.observed_vs_matrices ||
           !call.observed_live_eor_effect_pass)continue;
        const auto x=native::ResourcesAtDraw(*call.observed_vs_matrices);
        active_source_passes.insert(SourcePassKey{
            x.device,x.vertex_declaration,x.vertex_shader,x.pixel_shader,
            x.rt0,x.depth,call.observed_live_eor_effect_pass->effect_hash,
            call.observed_live_eor_effect_pass->pass_index});
      }
      std::vector<uint32_t> relevant;
      relevant.reserve(owner_catalog->verified_candidates.size());
      for(const auto& candidate:owner_catalog->verified_candidates) {
        if(!candidate.original_guest_resource_to_material_mapping_verified)
          continue;
        const auto& original=std::visit([](const auto& sub)
            ->const native::VerifiedDrawSource& {return sub.native_draw;},
            candidate.original_source_submission);
        if(!original.original_model_hash ||
           !original.engine_draw_verified ||
           !original.mesh_material_verified)
          continue;
        const auto& expected=candidate.original_resources;
        if(active_source_passes.contains(SourcePassKey{
               expected.device,expected.vertex_declaration,
               expected.vertex_shader,expected.pixel_shader,
               expected.rt0,expected.depth,original.original_effect_hash,
               original.original_pass_index}))
          relevant.push_back(original.original_model_hash);
      }
      std::sort(relevant.begin(),relevant.end());
      relevant.erase(std::unique(relevant.begin(),relevant.end()),relevant.end());
      if(!relevant.empty()) {
        if(owner_catalog->source_models) {
          if(!owner_catalog->direct_gpu ||
             !owner_catalog->source_models->UsesGpuBackend(
                 owner_catalog->direct_gpu->OriginalModelUploader()))
            return Result::kNotReady;
          owner_catalog->source_models->RequestSourceVerifiedModels(relevant,true);
        } else owner_catalog->request_source_models(relevant);
      }
    } catch (...) {
      return Result::kNotReady; // a failed source I/O request is not a Draw
    }
  }
  // Advance real source model GPU uploads on this sole Vulkan worker.
  // Merely queueing model hashes on an I/O thread never creates resident
  // position/index/attribute buffers needed by DrawIndexed.
  if(owner_catalog) {
    try {
      if(owner_catalog->source_models) {
        if(!owner_catalog->direct_gpu)return Result::kNotReady;
        AdvanceOriginalMeshUploads(*owner_catalog->source_models,*owner_catalog->direct_gpu);
      } else if(owner_catalog->advance_source_model_uploads)
        owner_catalog->advance_source_model_uploads();
    } catch (...) {return Result::kNotReady;}
  }
  // Source-verified GPU models are uploaded and adopted above. Build real
  // original Vulkan ShaderEffects materials only NOW, after the current
  // frame's stream tick; otherwise the material producer sees an empty
  // GPU residency table and first world Draw can never become eligible.
  // Populate newly source-authenticated material passes directly into
  // the SAME production Vulkan backend before this frame's DrawIndexed.
  // The original game can stream city textures and ShaderEffects after
  // the GPU backend is first constructed, without rebuilding the window.
  if(owner_catalog && owner_catalog->publish_original_material_passes) {
    if(!owner_catalog->direct_gpu ||
       owner_catalog->gpu.get()!=owner_catalog->direct_gpu.get())
      return Result::kNotReady;
    try {
      owner_catalog->publish_original_material_passes(
          *owner_catalog->direct_gpu,*source_snapshot);
    } catch (...) {
      return Result::kNotReady;
    }
  }
  // A source-only bootstrap deliberately has no verified material
  // owner yet. The real model I/O and GPU attribute uploads above MUST
  // run, but calling the expensive all-Draw witness resolver against an
  // empty catalog cannot produce a real scene. A later source factory
  // refresh may supply verified owners after residency and ShaderEffects
  // are ready; until then the existing renderer keeps ownership of Present.
  if(owner_catalog && owner_catalog->prepare_live_original_draws) {
    if(!host.swap_chain() || !owner_catalog->accepts_live_source)return Result::kNotReady;
    const auto& swap=host.swap_chain()->GetDesc();
    if(!owner_catalog->accepts_live_source(*source_snapshot,{swap.Width,swap.Height}))
      return Result::kNotReady;
    return ReplayVerifiedMixedOriginalFrame(host,std::move(owner_catalog),
        std::move(source_snapshot),source_sequence,independent_claimed);
  }
  if(owner_catalog && owner_catalog->verified_candidates.empty() &&
     owner_catalog->verified_font_materials.empty())
    return Result::kNotReady;
  // Original EFontShader geometry is transient by design. Route the
  // ENTIRE original Present through the mixed native Vulkan batch rather
  // than trying to disguise font glyphs as archived world-model meshes.
  const bool contains_original_font=std::any_of(
      source_snapshot->calls.begin(),source_snapshot->calls.end(),
      [](const RawDraw& call) {
        return call.original_engine_shader_method &&
          call.original_engine_shader_method->original_shader_method_guest==
              0x8266A0C0u;
      });
  if(contains_original_font) {
    if(!owner_catalog || !owner_catalog->direct_gpu ||
       owner_catalog->verified_font_materials.empty())
      return Result::kNotReady; // preserve NativeX display, never drop UI
    return ReplayVerifiedMixedOriginalFrame(
        host,std::move(owner_catalog),std::move(source_snapshot),
        source_sequence);
  }
  if(owner_catalog &&
     (!ready || ready->original_frame_sequence!=source_sequence)) {
    // The render worker, not the guest PPC Present thread, performs the
    // source-bound witness selection and retains current GPU residents.
    // This is a real new per-frame producer; no cached guest shader
    // instance from the preceding frame can be reused as an authority.
    if(owner_catalog->device!=&host || !host.swap_chain())
      return Result::kNotReady;
    const auto resident_lookup=owner_catalog->resident_lookup ?
        owner_catalog->resident_lookup :
        (owner_catalog->source_models ?
          native::VerifiedOriginalResidentLookup{
            [stream=owner_catalog->source_models](uint32_t hash) {
              return stream->ResidentShared(hash);
            }} : native::VerifiedOriginalResidentLookup{});
    if(!resident_lookup)return Result::kNotReady;
    try {
      auto frame=std::make_shared<VerifiedWorkerEorFrame>();
      frame->original_frame_sequence=source_sequence;
      frame->device=owner_catalog->device;
      frame->output=owner_catalog->output;
      frame->witnesses=owner_catalog->verified_candidates;
      frame->gpu=owner_catalog->gpu;
      frame->owner_catalog=owner_catalog;
      if(!InstallVerifiedWorkerEorFrameWithResidents(
             std::move(frame),resident_lookup))
        return Result::kNotReady;
    } catch (...) {
      return Result::kNotReady; // no Vulkan state touched
    }
    {
      std::lock_guard lock(g_mutex);
      ready=g_verified_worker_frame;
      const auto latest=g_frames_for_native.Peek();
      if(!ready || !latest || latest->sequence!=source_sequence ||
         ready->original_frame_sequence!=source_sequence ||
         ready->owner_catalog!=owner_catalog ||
         g_worker_owner_catalog!=owner_catalog ||
         g_frames_for_native.MissedSinceTake()!=0)
        return Result::kNotReady;
    }
  }
  if(!ready || source_sequence!=ready->original_frame_sequence ||
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
    if(ready->owner_catalog && ready->owner_catalog->direct_gpu &&
       ready->gpu.get()==ready->owner_catalog->direct_gpu.get())
      ready->owner_catalog->direct_gpu->AdoptVerifiedResidentWorldMeshes(residents);
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
    // HEORVRT1 is a real source-program bank, NOT automatically a
    // live XDK shader-object identity. Refuse any Draw without TWO exact
    // original ShaderContainer header proofs, obtained from a separately
    // source-verified object->header ABI and sampled at this Draw.
    // Comparison is performed before creating any host GPU state.
    if(ready->virtual_ids) {
      for(size_t i=0;i<source.size();++i) {
        const auto& guest=source_snapshot->calls[i];
        const auto& draw=source[i];
        if(!guest.observed_vs_matrices ||
           !draw.original_live_vertex_shader_header ||
           !draw.original_live_pixel_shader_header)
          return Result::kNotReady;
        const auto& live=*guest.observed_vs_matrices;
        const auto recovered_effect=aot::ResolveOriginalLiveEorEffectPass(
            *ready->virtual_ids,*ready->effects,
            *draw.original_live_vertex_shader_header,
            *draw.original_live_pixel_shader_header,
            live.original_vs_guest_object,live.original_ps_guest_object,
            source_snapshot->sequence,static_cast<uint32_t>(i));
        if(!recovered_effect || !guest.observed_live_eor_effect_pass ||
           guest.observed_live_eor_effect_pass->effect_hash!=
               recovered_effect->effect_hash ||
           guest.observed_live_eor_effect_pass->pass_index!=
               recovered_effect->pass_index ||
           recovered_effect->effect_hash!=draw.original_effect_hash ||
           recovered_effect->pass_index!=draw.original_pass_index ||
           !aot::MatchesOriginalLiveXdkShaderSource(
                *ready->virtual_ids,*draw.original_live_vertex_shader_header,
                live.original_vs_guest_object,aot::Stage::Vertex,
                draw.vertex_source_sha256,source_snapshot->sequence,
                static_cast<uint32_t>(i)) ||
           !aot::MatchesOriginalLiveXdkShaderSource(
                *ready->virtual_ids,*draw.original_live_pixel_shader_header,
                live.original_ps_guest_object,aot::Stage::Pixel,
                draw.pixel_source_sha256,source_snapshot->sequence,
                static_cast<uint32_t>(i)))
          return Result::kNotReady;
      }
    }
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
