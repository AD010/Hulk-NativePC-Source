# Hulk NativePC — Native D3D-layer strategy from verified recomps (2026-11-11)

This is a **source-only architecture and research decision**, NOT an implemented
renderer, fixed gameplay, verified 60 FPS or working GPU-image claim. It adds no
XEX, game files, shader bytes, TU binaries, or unlicensed code.

Scope: original **USA** The Incredible Hulk (2008) XEX,
SHA-256 **346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0**,
XDK **2.0.6534**. Existing repo: AD010/Hulk-NativePC-Source. Existing
one Windows workflow and user-owned game assets stay unchanged.

## Executive architecture finding

**Move the source-of-truth for drawing upward:** from trying to reconstruct
late PM4 command-ring and Xenos attachment semantics, toward the game's
original D3D/XDK API boundary (or verified Edge of Reality render submissions).
Retain recompiled guest game logic and the validated USA ABI. Render geometry,
textures, shader programs, color/depth targets and resolves with host Vulkan
(or another expressly selected native backend), not an Xenos GPU emulator.

This is a *candidate migration path*, not a claim that Hulk's D3D ABI or
resource lifecycle has already been recovered. Do not discard working
NativeXRenderer code. First prove the higher-level call/state contract and
pixel parity; only then retire PM4 host drawing and finally the GPU
command-processor/synchronization dependency.

The native graphics API is a SEPARATE choice from the capture boundary:
- Preserve the existing NativeXRenderer + DiligentCore/Vulkan baseline and
  the user's native Vulkan target during experiments.
- An earlier note, HULK_D3D11_PRIMARY_RENDERER_DECISION_2026-11-09.md, proposes
  D3D11 FL11_0 for an alternative future renderer. It is not implemented and
  does not prove D3D11 fixes Hulk's missing frames. The API choice should
  be decided by shader/RT parity and GPU tests, not copied from another title.
- Reject a "clean looking" renderer that still needs Xenia GPU emulation,
  EDRAM pixel round-trips, or a virtual gamepad as its final architecture.

## Audited source projects — specific mechanisms and limits

| Project / implementation | Proven source mechanism | Adopt for Hulk, not copied code |
| --- | --- | --- |
| Burnout Revenge / Xerenge Plume | rexglue/src/plume_renderer/plume_graphics_system.h: implements IGraphicsSystem and guest D3D draw/resolve/clear/shader callbacks; rexglue/src/plume_renderer/plume_shader_cache.h: offline shader microcode/SPIR-V index and prewarming. README says Plume takes original D3D calls rather than emulating the Xenos command processor; xenos is retained only as a diagnostic/reference backend. Different US/EU XEXs are rebuilt independently. | Candidate GPU-interface boundary, per-draw owned data, Vulkan backend, shader hash manifests, independent ABI validation; Burnout's engine layout and shader patches are NOT Hulk's. |
| re:Blue / Blue Dragon | src/gpu/hooks/draw.cpp, device.cpp, resource.cpp: typed REX_HOOK entrypoints for DrawVertices/Indexed/UP, Clear, Resolve, tiling, resource create/Lock/Release and Swap. src/gpu/hooks/device.cpp documents PPC floating-point call argument occupying a GPR slot: a wrong Clear stencil argument corrupts scene output. Plume resources and state are host-owned. | Closest code organization template for Hulk's XDK 6534 layer; explicitly prove every ABI signature, even for f1 + r-register interleaving. Don't reuse Blue Dragon addresses. BSD-3-Clause compliance if any source is adapted. |
| Dante's Inferno / Hell's Gate | docs/NATIVE_RENDERER_DESIGN.md and src/renderer/d3d_hooks.cpp: D3D-layer replacement, per-draw guest state verifier, native DiligentCore renderer; staged M0-M6. M2 compared shader-read registers of each draw with original GPU state before drawing. Sync-only command processor was temporary; elimination of remaining ring is a later distinct milestone. Read-only F8 hooks trace call ABI. | **Highest-value migration playbook**: USA D3D call map -> real shadow/PM4 diff -> native clears/resolves -> shaders/textures -> optional sync-only phase -> remove CP. Do not confuse Dante's verified addresses, device offsets or one-tile assumptions with Hulk's. |
| Sonic Unleashed Recompiled | Game-specific native renderer from draw calls; XenosRecomp AOT shader translation; pipeline construction tied to asset/stream loading; distinct internal resolution and game timing/physics patches; title update and region matching explicit in README. | Shader precompile and loading integration, genuine native game enhancements. Some frame-timing bugs remain even in this mature project. GPL source needs legal review before copying into MIT project. |
| The Darkness Recomp | RENDERING.md: engine-native geometry, completed shader/device state, D3D11, independent owned geometry and image lifetimes; partial color/depth/stencil clears preserve unaffected pixels; render-vs-present measurements separate. | Explicit native resource lifetime, pass-level clear, pixel contract and frametime tests, not its D3D11 engine code or VC++ DLL bundle. GPLv3 source cannot silently merge into Hulk's MIT sources. |
| Condemned 2 Recomp | src/condemned2_input.cpp has native input driver, direct native/mixed/disabled mouse modes, game Input_* imports, many configurable keybinds. Launcher and release support Title Update handling and graphics settings. | Find Hulk's actual camera and input manager call sites; direct yaw/pitch into validated game state, with cutscene/menu handover. Condemned is a different engine; no address reuse. |
| Spider-Man 3 Recomp | Dedicated Vulkan renderer, 1440p internal output, city streaming slots and independent view distance; releases report 60 FPS cap. README warns current public SDK does not reproduce the latest release. | Once frames are correct, derive Hulk's own EoR zone loader, true distance, scene MSAA and 60 Hz physics; do not paste another game's table lengths/offsets. |
| Edge of Time Recomp | Shader translation via XenosRecomp fork; native D3D12/Vulkan renderer reported, but public build guide says full native renderer is NOT in public source. | Surface-scale taxonomy and shader coverage ideas, not a reproducible renderer dependency. |
| King Kong Recompiled | Own D3D12/Vulkan backend, precompile shader packs, separate "Original Xbox 360" vs "Remastered" graphics settings, native mouse and frame-rate options. README cautions full campaign playthrough unconfirmed. | Two honest preset levels (parity/remaster), pipeline artifact provenance, user-facing quality settings, do not add GI/RT before Hulk's base image is correct. |

Source addresses (inspect exact upstream revision, never paste guest addresses):
- https://github.com/shipa-2/Xerenge
- https://github.com/shipa-2/Xerenge/tree/main/rexglue/src/plume_renderer
- https://github.com/zolaware/reblue/tree/main/src/gpu/hooks
- https://github.com/florinp93/hells-gate-recomp/blob/main/docs/NATIVE_RENDERER_DESIGN.md
- https://github.com/florinp93/hells-gate-recomp/blob/main/src/renderer/d3d_hooks.cpp
- https://github.com/hedge-dev/UnleashedRecomp
- https://github.com/portingpete/The-Darkness-Recomp/blob/main/RENDERING.md
- https://github.com/psxrestore/Condemned2Recomp/blob/main/src/condemned2_input.cpp
- https://github.com/b4ttycre4se/Spider-Man-3-Recomp
- https://github.com/goliathret/EdgeOfTimeRecomp/blob/main/docs/BUILDING.md
- https://github.com/TekRantGaming/king-kong-recompiled

## Existing Hulk evidence and boundary gaps (11 Nov)

Already in native_port/hulk_native_x_profile.ini:
- USA candidate GPU entrypoints:
  DrawVertices 0x8288DF30, DrawIndexedVertices 0x8288E318;
  Resolve 0x828897F8; SetRenderTarget 0x8287F2C0;
  SetDepthStencilSurface 0x8287F628; BeginTiling 0x8288A900;
  EndTiling 0x8288AD98; Present 0x82880778.
- Declared XDK device offsets include fetch constants +1152,
  VS float constants +1920, PS +6016, and GPU register shadow +10368.
  **Declared is not equivalent to verified for ALL ABI parameters, paths
  or shader forms.** Clear/ClearF, UP draws, shader object lifecycle,
  resource Lock/Unlock/Release, fence/interrupt and CPU readback still
  require evidence from the USA image.
- In user log #56: screen-size IEEE754 1280/720 bits appear in texture
  fetch slots; invalid 1D textures, unsupported primitive values and
  27-dword vertex shader analysis faults coexist with RT/depth mismatch.
  Those are SYMPTOMS; not proof which C++ class first corrupted the state.
- Three user RenderDoc streams contain 5,375 Draw commands and 236
  vkCmdBeginRendering passes; exactly 3 host loadOp=CLEAR attachments,
  at output resolution, were observed. This does **not** rule out clears
  drawn as RECTLIST shaders; Pixel History or before/after attachment
  readback is needed for ClearF parity.
- Current CMake target still selects rexglue_setup_target(... GPU_PLUGINS
  xenos), and package validator requires rexgpu-xenos.dll. The existing
  NativeX GPU observer is useful for comparisons, but the FINAL package
  must have an independently operating native GPU/system interface
  if the user requires no Xenos GPU dependencies at all.
- Old PAL/devkit XEX analysis is only comparative. Never inherit PPC
  hook addresses from PAL into the USA target.

## Proposed boundary contract

Guest D3D/XDK call (exact USA guest address, type, ABI, object lifetime)
  -> HulkGuestDrawState (typed, immutable after call)
  -> HulkHostFrame (ordered clears, draws, resolves, present; owned memory)
  -> Vulkan/DiligentCore render targets and shaders (no fake EDRAM pixels)
  -> Frame present with real 16:10 window sizing and native scaling.

Per-draw state needed for a faithful native renderer:
- render pass ID, scene depth, RT0-3 format/size/sample count and writable
  masks, clear flags/rects and post-resolve clears;
- VS/PS program identity, actual ucode source and size, shader constants
  and texture fetch slot binding provenance; do not infer a texture
  descriptor from unrelated screen-dimension float words;
- vertex declaration, index type, starting offset, primitive type,
  validated vertex/index ownership, exact input layout and endianness;
- blend, alpha, depth/stencil, culling, scissor, viewport, bias and color
  space; frame/guest-swap clock and resource lifetime events;
- CPU-visible resolve output and GPU synchronization/queries when required.
The record can be materialized as address-only metadata in public source
tests, but original guest shaders and asset bytes MUST remain private.

## Migration milestones — each requires a hard exit criterion

**H0: Region/ABI proof, no render change.**
Run exact-USA AOT reverse-call graph; identify original Clear/ClearF,
DrawVerticesUP/Begin/End, shader binding/lifetime, resource creation,
SetTexture, window/swap, fence, readback and engine scene submission.
Verify disassembly, r3..r10/f1/f2, call-stack provenance and object bytes
read-only. Exit: every proposed hook has a USA body, a confirmed signature
and a read-only regression; unknown methods remain UNKNOWN, never guessed.
Existing eight hooks can seed this audit but are not complete coverage.

**H1: Per-draw state oracle and source-of-truth test.**
At each XDK high-level Draw capture the actual guest device shadow and
compare it against the GPU register file immediately before its PM4 draw.
Compare only registers consumed by the shader and active render pass,
plus shader hash/index source; classify state provenance as XDK, PM4
SET, PM4 LOAD, or unknown. Exit: captured menu + full first-world frame
show zero unexplained mismatches. A late fetch 1280.0/720.0 error must
be attributed to a specific writer or packet parser. Use existing
observer-only mode, not a shipping Xenos dependency.

**H2: Native frame skeleton with correct synchronization.**
Exercise real game calls with a Vulkan RT/depth registry, Clear/Resolve/
Present and readbacks but no world Draws. Preserve GPU fences and
vblank ordering; optionally use a temporary SYNC-ONLY command processor
in diagnostic builds, explicitly tracked as unfinished work.
Exit: menu, loading, pause, save, minimize/restore and first level
survive without stalls, with identical pass order/surface identities
and no use-after-free. Do not duplicate a competing Vulkan swapchain.

**H3: Native shaders, geometry and textures.**
Establish complete shader inventory from user-owned USA assets and
runtime-assembled source as needed. Translate/prewarm known variants,
prefer at asset-load boundaries rather than first visible Draw. Verify
vertex/index byte order, 16-bit index offset alignment, texture
tiling/mips, fetch slots, signed formats and samplers. Exit: all known
menu + first-world Draws with correct shaders, no ignored required
draws, and no invalid fetch descriptor; RenderDoc inspect first broken
pixel versus an authenticated reference. This phase can reuse verified
ReXGlue shader decoder components if their host binding model is
adapted correctly; do not copy title-specific translated shader bytes.

**H4: Full image parity and remove Xenos GPU runtime.**
Validate all scene passes, shadow maps, water/particles, color/depth,
1x/4x transitions and 16:10 viewport, exact source/destination
resolves and partial clears. Complete CPU-visible readbacks,
guest fence, query and interrupt handling on the native timeline.
Retire reliance on PM4 parsing and rexgpu-xenos.dll for normal
runtime (allow a separate DEBUG/forensic baseline, never hidden
inside the end-user release). Exit: repeated gameplay/cutscene
captures with no black/green/quarter-frame, corrupted geometry,
missing scene, texture-fetch or shader failure; release dependency
inspection verifies no runtime Xenos GPU plugin is required.

**H5: Official-port/remaster polish, only after H4.**
Independent real rendering scale and per-pass scale classification,
2560x1600/175% DPI, mouse yaw/pitch via VERIFIED EoR camera setters,
correct game simulation and physics at 60+ Hz, stable Alt+Tab,
LOD/city streaming, display/HDR if tested, title-update equivalence
only after USA compatibility checks, original PC key prompts, settings,
localization and offline install. Provide Original Xbox 360 and
Remastered presets with explicit features; optional effects are not
allowed to conceal parity bugs.

## Validation and build safety

Use current AD010/Hulk-NativePC-Source main and ONE existing Windows
Release workflow. No new public assets, workflow YAML, new repositories
or bulk copying another GPL project into MIT code.

Every milestone needs:
1. pin hashes for upstream SDK, guest USA XEX, renderer and shaders;
2. unit/integration pass on the same production source; tests against
   synthetically pasted strings alone are not GPU proof;
3. reproducible scene input: menu, street, rooftop, driving/vehicle,
   water, night/lighting, first cutscene, save/load;
4. actual GPU capture, draw counters plus original engine/PM4 state
   comparison, before/after RT pixel evidence;
5. 1280x720 baseline + true 2560x1440 internal 2x on 2560x1600
   display with correct 80px top/bottom letterbox if choosing 16:9;
6. no shader compilation dropouts at scene loading; separate game
   simulation FPS from host Present FPS;
7. private source-free package only after compilation, with all
   dependencies and no source/user games bundled.

Failure at one gate is a diagnostic result, not permission to add
arbitrary Xenos register overrides, unverified EDRAM aliases, guessed
hook addresses, CRT DLLs or shader skip lists.

## What this audit does NOT authorize

- It does not change today's NativeX renderer, enable a new backend,
  modify title-update data or certify current CI build #57.
- A new Direct3D 11 rendering proposal is NOT an automatically
  better solution than Vulkan; first confirm the guest D3D contract.
- Dante's M5 parity was still in progress and sync-only CP was a
  temporary development dependency, not proof that replacing the GPU
  interface is a one-line change.
- Burnout, Blue Dragon, King Kong, Sonic and Hulk use distinct engines:
  C++ patterns, checks and contracts may transfer; XEX addresses,
  shader blobs, EDRAM layouts and game resources may not.

**Immediate actionable next edit:** implement source-only read-only
USA-XEX D3D boundary/ABI map and per-draw shadow observer, starting
with verified existing eight XDK callbacks and finding true ClearF,
DrawUP, resource Lock/Unlock, shader and fence methods. Do not attempt
a new renderer until H0/H1 acceptance evidence exists.
