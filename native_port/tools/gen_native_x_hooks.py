#!/usr/bin/env python3
"""Generate hooking wrappers for known guest addresses (NOT game hook names).
No rendering is redirected unless a fully validated XDK profile maps every method.
Usage: python tools/gen_native_x_hooks.py native_hook_addresses.txt analysis/guest_host_mapping.csv output.cpp
"""
import sys,re,csv,configparser,hashlib
from pathlib import Path
if len(sys.argv) not in (4, 5, 6): raise SystemExit(__doc__)
# These are verified original USA XDK functions, not historical PE RVAs.
# The build validates the exact owner XEX and generated symbol definitions.
# Kick 82886428: SHA256 b84fc4331563e192a0b7733d833fa594b1c79e5bbc400167eb673abc8b541da6
# New  82885FF8: SHA256 e959315c50eedbcb3375eb500bdb3e4fca242a2eb5988e4a02064e7697bdf380
SOURCE_PINNED_SEGMENT_HOOKS = {"82886428": "KickSegment", "82885FF8": "NewSegment"}
SOURCE_EOR_CAMERA_GETTERS = {"82338A00", "82338A18"}
SOURCE_EOR_CAMERA_SPEED = {"82339C18", "82339C90", "82338AE8", "82338B20"}
SOURCE_EOR_HAVOK_FLOOR = {"82738B58"}
SOURCE_EOR_PC_MENU_EVENTS = {"825BAAC8", "825BAC08"}
SOURCE_EOR_PC_MENU_CALLS = {"825B8270", "825B9528", "825B9D98", "8271D340", "8271AA60"}
SOURCE_EOR_ACTUAL_ITEM_DRAW_METHODS = {"82667648", "826676C0"}
SOURCE_EOR_FMOD_UPDATE = {"82841DB0"}
def verify_pinned_codegen(directory, addresses):
    if not addresses:
        return
    xex = directory.parent.parent / "game" / "default.xex"
    if hashlib.sha256(xex.read_bytes()).hexdigest() != "346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0":
        raise SystemExit("New hook source is not the pinned original USA XEX")
    definitions = dict.fromkeys(addresses, 0)
    pattern = re.compile(r"(?m)^DEFINE_REX_FUNC\(sub_([0-9a-fA-F]{8})\)")
    for file in directory.glob("*_recomp.*.cpp"):
        for match in pattern.finditer(file.read_text(encoding="utf-8")):
            address = match.group(1).upper()
            if address in definitions:
                definitions[address] += 1
    if any(count != 1 for count in definitions.values()):
        raise SystemExit("Missing or duplicate original pinned codegen body: " + str(definitions))
    print("original USA pinned codegen bodies verified:", len(definitions))
# When generating the actual executable, the profile is a required source
# dependency. Developer's old 3-argument probe remains supported, but CMake
# MUST supply the embedded profile as the fourth argument and fail closed
# if it names even one guest function without a compiled PPC wrapper.
entries=Path(sys.argv[1]).read_text(encoding='utf-8').splitlines()
valid={r['guest_address'].removeprefix('0x').upper() for r in csv.DictReader(open(sys.argv[2],encoding='utf-8-sig'))}
a=[]
for line in entries:
 line=line.split('#',1)[0].strip().removeprefix('0x').upper()
 if not line:continue
 if not re.fullmatch('[89A-F][0-9A-F]{7}',line) or (line not in valid and line not in SOURCE_PINNED_SEGMENT_HOOKS and line not in SOURCE_EOR_CAMERA_GETTERS and line not in SOURCE_EOR_HAVOK_FLOOR and line not in SOURCE_EOR_CAMERA_SPEED and line not in SOURCE_EOR_PC_MENU_EVENTS and line not in SOURCE_EOR_ACTUAL_ITEM_DRAW_METHODS and line not in SOURCE_EOR_FMOD_UPDATE):raise SystemExit('Not a mapped Hulk guest address: '+line)
 if line in a:raise SystemExit('Duplicate hook: '+line)
 a.append(line)
if len(sys.argv) >= 5:
    cfg = configparser.ConfigParser(interpolation=None)
    if not cfg.read(sys.argv[4], encoding="utf-8") or not cfg.has_section("hooks"):
        raise SystemExit("NativeX embedded profile missing [hooks]: " + sys.argv[4])
    mandatory = {
        key.removeprefix("0x").upper(): method
        for key, method in cfg.items("hooks")
    }
    missing = sorted(k for k in mandatory if k not in a)
    if missing:
        raise SystemExit("Profile has uncompiled NativeX PPC hooks: " +
                         ", ".join("0x" + key + "=" + mandatory[key] for key in missing))
    for addr, role in SOURCE_PINNED_SEGMENT_HOOKS.items():
        if addr in a and mandatory.get(addr) != role:
            raise SystemExit("Wrong original segment hook role: " + addr)
    print("embedded profile hook addresses verified:", len(mandatory))
if len(sys.argv) == 6:
    pinned = set(a) & (SOURCE_PINNED_SEGMENT_HOOKS.keys() | SOURCE_EOR_CAMERA_GETTERS | SOURCE_EOR_HAVOK_FLOOR | SOURCE_EOR_CAMERA_SPEED | SOURCE_EOR_PC_MENU_EVENTS | SOURCE_EOR_ACTUAL_ITEM_DRAW_METHODS | SOURCE_EOR_FMOD_UPDATE)
    if set(a) & SOURCE_EOR_PC_MENU_EVENTS:
        pinned |= SOURCE_EOR_PC_MENU_CALLS
    verify_pinned_codegen(Path(sys.argv[5]), pinned)

# These original USA PPC methods are NOT XDK APIs. Their named engine
# shader-vtable entry identity was verified against the user's actual
# decrypted USA PPC image; the generic NativeX Enter/Leave path must NOT
# run for these game-class virtual methods.
SOURCE_EOR_SHADER_METHODS = {
    # 52 source-matched original class vtables, 20 unique guest methods.
    # 826432F0 is shared by 33 classes: the live BE32 this[0] disambiguates.
    "826432F0",
    "82651978",
    "82652420",
    "82652B60",
    "8265B420",
    "82662780",
    "82665288",
    "82665500",
    "82668D50",
    "82669630",
    "8266A0C0",
    "8266C668",
    "8266CC28",
    "8266D6D0",
    "8266DDD0",
    "8266E520",
    "8266EB88",
    "8266F198",
    "8266FEF8",
    "82670738",
}
# Original USA 0x82642A88 virtual +44 per-item renderer, 52 shader
# classes and 22 distinct guest AOT entries. 27 classes share
# 82642370. Never infer model/surface hash from callsite.
SOURCE_EOR_RENDER_ITEM_METHODS = {
    "8263DCD0",
    "82642370",
    "82662108",
    "82663270",
    "82663AA8",
    "82663EF8",
    "82664338",
    "82664630",
    "82664EE0",
    "82664F88",
    "82665160",
    "826656C8",
    "82665988",
    "82666090",
    "826664F8",
    "82666808",
    "82666C70",
    "826672D0",
    "8266B218",
    "8266B588",
    "8266B8B8",
    "8266C8D0",
}
SOURCE_INLINE_END = {"8288D8F8"}
SOURCE_EOR_VM_METHODS = {"8264C798"}  # original registered state_r global
SOURCE_EOR_CAMERA_OPTION_METHODS = {"823399E8", "82339A08", "82339A30"}
SOURCE_EOR_SUBTITLE_GETTER = {"82338A38"}
SOURCE_EOR_AUDIO_MENU_METHODS = {"82339D08","82339D20","82339D90","82339E28"}
SOURCE_EOR_HAVOK_STEP_METHODS = {"82508F60", "82509C18"}
SOURCE_EOR_HAVOK_CATCHUP_METHODS = SOURCE_EOR_HAVOK_FLOOR
# 0x825F8A50 is original Decal item-material setter, independently verified
# from PPC bl 0x82642CB0 and its native shader context ABI.
SOURCE_EOR_MATERIAL_METHODS = {"825F8A50"}
# True original 92-byte per-pass EoR dual-stage source resolver.
SOURCE_EOR_PASS_METHODS = {"825F84C0"}
if len(sys.argv) >= 5 and (set(mandatory) &
                            (SOURCE_INLINE_END | SOURCE_EOR_SHADER_METHODS | SOURCE_EOR_PC_MENU_EVENTS |
                             SOURCE_EOR_VM_METHODS | SOURCE_EOR_CAMERA_SPEED | SOURCE_EOR_CAMERA_OPTION_METHODS | SOURCE_EOR_CAMERA_GETTERS | SOURCE_EOR_SUBTITLE_GETTER | SOURCE_EOR_AUDIO_MENU_METHODS |
                             SOURCE_EOR_HAVOK_STEP_METHODS | SOURCE_EOR_HAVOK_CATCHUP_METHODS | SOURCE_EOR_MATERIAL_METHODS |
                             SOURCE_EOR_PASS_METHODS |
                             SOURCE_EOR_RENDER_ITEM_METHODS | SOURCE_EOR_ACTUAL_ITEM_DRAW_METHODS | SOURCE_EOR_FMOD_UPDATE)):
    raise SystemExit("Original EoR shader/VM method incorrectly declared an XDK profile entry")

h="""// GENERATED by gen_native_x_hooks.py. Do not assign method roles without evidence.
#include <native_x_renderer/hooks.h>
#include "native_x_hulk_profile.h"
#include "eor_xdk_hook_bridge.h"
// Profiling the PPC wrapper ABI on EVERY call is unnecessary in the
// Release port. The developer-only source and calls are both removed
// unless the explicit HULK_NATIVE_GPU_HOOK_PROBE build option is enabled.
#if defined(HULK_NATIVE_GPU_HOOK_PROBE)
#include "native_gpu_hook_probe.h"
#define HULK_GPU_PROBE_HIT(index, addr, ctx) ::hulk::gpu_probe::Hit(index, addr, ctx)
#else
#define HULK_GPU_PROBE_HIT(index, addr, ctx) ((void)0)
#endif
#define HULK_NATIVE_X_HOOK(addr,index) \\
    REX_EXTERN(__imp__sub_##addr); \\
    REX_HOOK_RAW(sub_##addr) { \\
        HULK_GPU_PROBE_HIT(index, 0x##addr, ctx); \\
        const auto type = ::hulk::nx::Lookup(0x##addr); \\
        if (::hulk::eor::xdk::CaptureEnabled()) { \\
            if (type == ::native_x_renderer::Call::kDrawVertices) \\
                ::hulk::eor::xdk::CapturePpcDraw(::hulk::eor::xdk::Kind::DrawVertices, 0x##addr, ctx, base); \\
            else if (type == ::native_x_renderer::Call::kDrawIndexedVertices) \\
                ::hulk::eor::xdk::CapturePpcDraw(::hulk::eor::xdk::Kind::DrawIndexedVertices, 0x##addr, ctx, base); \\
            else if (type == ::native_x_renderer::Call::kDrawVerticesUP) \\
                ::hulk::eor::xdk::CapturePpcDraw(::hulk::eor::xdk::Kind::DrawVerticesUP, 0x##addr, ctx, base); \\
            else if (type == ::native_x_renderer::Call::kSetRenderTarget) \\
                ::hulk::eor::xdk::CapturePpcCommand(::hulk::eor::xdk::CommandKind::SetRenderTarget, 0x##addr, ctx, base); \\
            else if (type == ::native_x_renderer::Call::kSetDepthStencilSurface) \\
                ::hulk::eor::xdk::CapturePpcCommand(::hulk::eor::xdk::CommandKind::SetDepthStencilSurface, 0x##addr, ctx, base); \\
            else if (type == ::native_x_renderer::Call::kClearF) \\
                ::hulk::eor::xdk::CapturePpcCommand(::hulk::eor::xdk::CommandKind::ClearF, 0x##addr, ctx, base); \\
            else if (type == ::native_x_renderer::Call::kResolve) \\
                ::hulk::eor::xdk::CapturePpcCommand(::hulk::eor::xdk::CommandKind::Resolve, 0x##addr, ctx, base); \\
            else if (type == ::native_x_renderer::Call::kBeginTiling) \\
                ::hulk::eor::xdk::CapturePpcCommand(::hulk::eor::xdk::CommandKind::BeginTiling, 0x##addr, ctx, base); \\
            else if (type == ::native_x_renderer::Call::kEndTiling) \\
                ::hulk::eor::xdk::CapturePpcCommand(::hulk::eor::xdk::CommandKind::EndTiling, 0x##addr, ctx, base); \\
            else if (type == ::native_x_renderer::Call::kPresent) \\
                ::hulk::eor::xdk::CapturePpcCommand(::hulk::eor::xdk::CommandKind::Present, 0x##addr, ctx, base); \\
        } \\
        if(type==::native_x_renderer::Call::kCount || !::native_x_renderer::detail::g_hooks_active) { \\
            __imp__sub_##addr(ctx,base); \\
            ::hulk::eor::xdk::EndPpcHook(type); return; \\
        } \\
        ::native_x_renderer::detail::Enter(type,ctx,base); \\
        __imp__sub_##addr(ctx,base); \\
        /* Publish a complete original Present snapshot BEFORE NativeX queues its */ \\
        /* frame to the worker. The worker may consume that Present immediately. */ \\
        ::hulk::eor::xdk::EndPpcHook(type); \\
        ::native_x_renderer::detail::Leave(type,ctx,base); \\
    }
"""
# Original EoR class virtual-method hooks are source-only READ-ONLY
# parents of any inner original XDK Draws. Never call NativeX Enter/Leave
# for a shader method (it is not XDK and owns no swapchain).
h += r"""
#define HULK_EOR_SHADER_TRACE_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        ::hulk::eor::xdk::ScopedOriginalEorShaderMethod original_engine_scope( \
            0x##addr,ctx,base); \
        __imp__sub_##addr(ctx,base); \
    }
"""
# The original VM state_r global is shared by UNRELATED game scripts.
# This wrapper only observes the original r3/VM context BEFORE calling
# the genuine native PPC implementation. It does not open PC Settings.
h += r"""
#define HULK_EOR_VM_STATE_TRACE_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        ::hulk::eor::xdk::ObserveOriginalEorVmState_r(0x##addr,ctx,base); \
        __imp__sub_##addr(ctx,base); \
    }
"""
# Genuine original EoR menu camera options. Capture before original call,
# then mirror only the confirmed original boolean after the call returns.
h += r"""
#define HULK_EOR_CAMERA_OPTION_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        const auto change=::hulk::eor::xdk::CaptureOriginalCameraMenuOption(0x##addr,ctx,base); \
        __imp__sub_##addr(ctx,base); \
        ::hulk::eor::xdk::FinishOriginalCameraMenuOption(change,base); \
    }
"""
# Original EoR camera-speed methods: capture the verified menu owner and
# update the PC option only after the original method returns.
h += r"""
#define HULK_EOR_CAMERA_SPEED_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        const auto e=::hulk::eor::xdk::CaptureOriginalCameraSpeed(0x##addr,ctx,base); \
        __imp__sub_##addr(ctx,base); \
        ::hulk::eor::xdk::FinishOriginalCameraSpeed(e,base); \
    }
"""
# Original camera restoration occurs before the genuine VM getter.
h += r"""
#define HULK_EOR_CAMERA_GETTER_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        ::hulk::eor::xdk::RestoreOriginalCameraGetter(0x##addr,ctx,base); \
        __imp__sub_##addr(ctx,base); \
    }
"""
# Subtitle restoration executes before the original VM getter.
h += r"""
#define HULK_EOR_SUBTITLE_GETTER_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        ::hulk::eor::xdk::RestoreOriginalSubtitleOption(ctx,base); \
        __imp__sub_##addr(ctx,base); \
    }
"""
# Original EoR volume callbacks always run their original guest body.
h += r"""
#define HULK_EOR_FMOD_UPDATE_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        const auto stamp=::hulk::eor::xdk::CaptureOriginalFmodUpdate(ctx,base); \
        __imp__sub_##addr(ctx,base); \
        ::hulk::eor::xdk::FinishOriginalFmodUpdate(stamp,ctx,base); \
    }
#define HULK_EOR_AUDIO_MENU_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        const auto change=::hulk::eor::xdk::CaptureOriginalAudioMenuOption(0x##addr,ctx,base); \
        __imp__sub_##addr(ctx,base); \
        ::hulk::eor::xdk::FinishOriginalAudioMenuOption(change,base); \
    }
"""
# Havok step boundaries are original CPU functions, never XDK calls.
h += r"""
#define HULK_EOR_HAVOK_STEP_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        ::hulk::eor::xdk::ObserveOriginalHavokStep(0x##addr,ctx,base); \
        __imp__sub_##addr(ctx,base); \
    }
"""
# Preserve original gameplay physics update and its worker completion
# barriers. Optional correction acts only on the outer fixed-step accumulator.
h += r"""
#define HULK_EOR_HAVOK_CATCHUP_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        ::hulk::eor::xdk::ApplyOriginalHavokCatchup(ctx,base); \
        __imp__sub_##addr(ctx,base); \
    }
"""
# Original item-material descriptor setter: observe this source PPC method
# before original call, but never invoke native Vulkan or change its state.
h += r"""
#define HULK_EOR_MATERIAL_TRACE_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        ::hulk::eor::xdk::ObserveOriginalEorMaterialDescriptor(0x##addr,ctx,base); \
        __imp__sub_##addr(ctx,base); \
    }
"""
# Preserve original PPC per-item renderer virtual method unchanged.
# Source LR and live node provenance distinguish preparation from real mesh submission.
h += r"""
#define HULK_EOR_RENDER_ITEM_TRACE_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        ::hulk::eor::xdk::ScopedOriginalEorRenderItem source_render_item(0x##addr,ctx,base); \
        __imp__sub_##addr(ctx,base); \
    }
"""
# Source original state-slot shader binding:
# Observe r3=material context and r4=1-based original pass, THEN call
# the untouched PPC implementation, which sets both original XDK stages.
h += r"""
#define HULK_EOR_SHADER_PASS_TRACE_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        ::hulk::eor::xdk::ObserveOriginalEorShaderPass(0x##addr,ctx,base); \
        __imp__sub_##addr(ctx,base); \
    }
"""
h += r"""
#define HULK_EOR_INLINE_END_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        ::hulk::eor::xdk::CapturePpcDraw(::hulk::eor::xdk::Kind::EndVertices, 0x##addr, ctx, base); \
        __imp__sub_##addr(ctx,base); \
    }
"""
h += r"""
#define HULK_EOR_PC_MENU_HOOK(addr) \
    REX_EXTERN(__imp__sub_##addr); \
    REX_HOOK_RAW(sub_##addr) { \
        ::hulk::eor::xdk::DispatchOriginalPcMenu(ctx,base,&__imp__sub_##addr,0x##addr==0x825BAC08u); \
    }
"""
Path(sys.argv[3]).parent.mkdir(parents=True,exist_ok=True)
generated = h + '\n'.join(
    'HULK_EOR_PC_MENU_HOOK('+addr+')' if addr in SOURCE_EOR_PC_MENU_EVENTS else
    'HULK_EOR_INLINE_END_HOOK('+addr+')' if addr in SOURCE_INLINE_END else
    'HULK_EOR_HAVOK_CATCHUP_HOOK('+addr+')' if addr in SOURCE_EOR_HAVOK_CATCHUP_METHODS else
    'HULK_EOR_HAVOK_STEP_HOOK('+addr+')' if addr in SOURCE_EOR_HAVOK_STEP_METHODS else
    'HULK_EOR_FMOD_UPDATE_HOOK('+addr+')' if addr in SOURCE_EOR_FMOD_UPDATE else
    'HULK_EOR_AUDIO_MENU_HOOK('+addr+')' if addr in SOURCE_EOR_AUDIO_MENU_METHODS else
    'HULK_EOR_CAMERA_SPEED_HOOK('+addr+')' if addr in SOURCE_EOR_CAMERA_SPEED else
    'HULK_EOR_CAMERA_GETTER_HOOK('+addr+')' if addr in SOURCE_EOR_CAMERA_GETTERS else
    'HULK_EOR_SUBTITLE_GETTER_HOOK('+addr+')' if addr in SOURCE_EOR_SUBTITLE_GETTER else
    'HULK_EOR_CAMERA_OPTION_HOOK('+addr+')' if addr in SOURCE_EOR_CAMERA_OPTION_METHODS else
    'HULK_EOR_VM_STATE_TRACE_HOOK('+addr+')'
    if addr in SOURCE_EOR_VM_METHODS
    else ('HULK_EOR_RENDER_ITEM_TRACE_HOOK('+addr+')'
          if addr in (SOURCE_EOR_RENDER_ITEM_METHODS | SOURCE_EOR_ACTUAL_ITEM_DRAW_METHODS)
          else ('HULK_EOR_SHADER_PASS_TRACE_HOOK('+addr+')'
                if addr in SOURCE_EOR_PASS_METHODS
          else ('HULK_EOR_MATERIAL_TRACE_HOOK('+addr+')'
                if addr in SOURCE_EOR_MATERIAL_METHODS
                else ('HULK_EOR_SHADER_TRACE_HOOK('+addr+')'
                      if addr in SOURCE_EOR_SHADER_METHODS
                      else 'HULK_NATIVE_X_HOOK('+addr+','+str(i)+')'))))
    for i,addr in enumerate(a)) + '\n'
generated += '\nnamespace hulk::nx {\nbool IsCompiledHook(uint32_t address) {\n  switch(address) {\n'
generated += ''.join('    case 0x'+addr+'u: return true;\n' for addr in a)
generated += '    default: return false;\n  }\n}\n}\n'
Path(sys.argv[3]).write_text(generated,encoding='utf-8')
print('generated',len(a),'hook sites in',sys.argv[3])
