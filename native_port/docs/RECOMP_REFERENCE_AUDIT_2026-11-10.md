# Hulk NativePC: reference-port engineering audit (10 November 2026)

This is a **source-code comparison and adoption plan**, not an assertion that the
listed enhancements already work in Hulk. Reference versions were checked on
10 November 2026. Do not add game binaries, proprietary assets or codegen output
to the public Hulk repository. Keep the current single Windows Release workflow.

## What is actually reusable

| Reference, exact implementation | What it teaches | Hulk adoption / restriction |
| --- | --- | --- |
| [King Kong](https://github.com/TekRantGaming/king-kong-recompiled/blob/main/native-renderer/backend/pipeline_demand.h), `pipeline_demand.h` | Game draw pipeline misses distinguished from critical depth/stencil/screen passes, which cannot always be skipped; background workers and persistent pipeline caches | **Started:** opt-in `native_x_renderer_hulk_wait_all_pipelines` diagnostic added to our existing NativeX path. It makes all shader/pipeline draws wait rather than dropping a scene draw while the compiler runs. Keep default off because GPU stalls are expensive. Compare same scene with both modes; only then implement targeted dependency classes. |
| [King Kong shader pipeline](https://github.com/TekRantGaming/king-kong-recompiled/blob/main/native-renderer/docs/shaders.md) | Original shader database, translator fixtures, SPIR-V validation and GPU-vs-reference-interpreter comparisons; prebuilt shader and pipeline packs | **High priority:** build a Hulk-owned shader inventory from legally supplied data, reject invalid `IM_LOAD_IMMEDIATE` microcode with an identified source, compare a minimal synthetic corpus and eventually prewarm validated pipelines. Cannot reuse King's Jade-specific shader containers or packs. |
| [King Kong high FPS analysis](https://github.com/TekRantGaming/king-kong-recompiled/blob/main/docs/high-frame-rate.md) | Compare clock/physics traces at 30 vs 60/120; change per-frame computations only after identifying exact functions | **Later:** Hulk's verified 30 Hz game clock and physics must be traced separately. King Kong addresses and timing functions are unrelated. |
| [King Kong mod system](https://github.com/TekRantGaming/king-kong-recompiled/blob/main/MODDING.md) | User-owned texture packs, manifests, version guards, reversible overrides, no original files modified | **Later:** an additive Hulk mod root with path sanitization and archive-index override tests after the EoR loader is understood. Do not insert unreadable copyrighted image packs. |
| [The Darkness D3D11](https://github.com/portingpete/The-Darkness-Recomp/blob/main/RENDERING.md) | Render target lifetime, engine-native device state, native pixel scaling, multiple resolution options, safe frame transitions | **High priority:** apply invariants to our NativeX RT0/depth/Resolve and 1x/4x sample-grid mapping; backend itself is a different engine and GPLv3. Do not copy GPL code into the MIT Hulk project without an explicit licensing decision. |
| [Estacado DX12](https://github.com/invinceble55-wq/Estacado-The-Darkness-Recomp) | Native mouse and settings launcher, optional HD texture packs, scale 1x-3x, shader-cache workflow | **Medium:** compare display and mod UX. Its XenonRecomp/DX12 integration is not a drop-in replacement for Hulk's ReXGlue+NativeX. |
| [Sonic Unleashed](https://github.com/hedge-dev/UnleashedRecomp/tree/main/UnleashedRecomp/mod) and [installer](https://github.com/hedge-dev/UnleashedRecomp/tree/main/UnleashedRecomp/install) | Read-only path remapping, mod priority, installer integrity checks, revision-aware TU and DLC | **High for later:** implement hash/region validation before Hulk Title Update; currently EU TU1 cannot be directly installed on pinned USA XEX. The Sonic game runtime and GPLv3 mod code are not drop-in. |
| [Spider-Man 3](https://github.com/b4ttycre4se/Spider-Man-3-Recomp) | City's original 8 region slots expanded to 13, building view distance 800 to 1500 game units, world tables expanded | **High for skyline:** first identify Hulk's active `ZoneStreamManager` and `HiResDataset` references, game memory budget and 30/60 Hz streaming traces. Those Spider-Man memory addresses, sizes and numbers must NOT be copied. This project gives no clear license for source reuse. |
| [Condemned 2](https://github.com/psxrestore/Condemned2Recomp/blob/main/src/condemned2_graphics.cpp) | ReXGlue CVars, live graphics hooks, FOV/shadow quality, input/launcher patterns | **Medium:** patterns fit our current SDK, but every game hook must be re-derived from Hulk's own XEX and verified; never copy addresses or commands verbatim. BSD-3-Clause attribution applies to any substantial reused source. |
| [Skate 3](https://github.com/mchughalex/skate3recomp) | Dynamic Native/Emulated renderer comparison and GPU failure fallback; source HLSL with offline SPIR-V | **Medium:** benchmark equal scenes and create a safe observer/golden-frame comparison. GPU backend hot-switching would require major changes; not currently safe for Hulk. |

## Cross-project priority, tied to measured Hulk evidence

1. **P0 GPU correctness, before any remaster effects.** In original Hulk log 173,
   image corruption coexists with bad texture-fetch descriptors, unsupported
   primitive types, mismatched packet-time RT0 identity, some bad Xenos shader
   analysis and incomplete command-buffer coverage. Fix chronological PM4 and
   GPU/XDK surface identity; verify per-frame 1x/4x resolves before raising scale.
   Do not hide errors with broad skip lists.
2. **P0 missing draws vs shader/pipeline warmup.** King Kong showed why dropped
   critical passes can cause visual errors. Hulk currently waits only for
   offscreen pipelines. This audit adds the reversible
   `native_x_renderer_hulk_wait_all_pipelines` **diagnostic only**. On the
   same location and camera, compare startup, first 120 frames, frame times,
   `stats.compiling`, `stats.translating` and screenshots with false/true.
   If the image does not improve, this is not the root cause.
3. **P1 true 2x Vulkan rendering.** Test 1280x720 guest to 2560x1440 host,
   actual scene/depth resolution, MSAA grid, shader pixel positions, resolve
   scale, full-screen postprocess, font/HUD. Do not mistake 720p bilinear
   presentation for native 1440p.
4. **P1 Manhattan streaming/LOD.** Compare ground, halfway up a building, roof,
   jump and night mission. Discover real city zone residency and asset names,
   then consider capacity increases and HLOD. Widening `CameraFarPlane`
   or removing fog without high-detail zones reveals empty grey boxes.
5. **P2 quality and usability.** Stable frame timing and physics,
   native camera mouse input, reliable window/DPI/Alt+Tab, menu settings,
   graphics presets, safe user-owned mod loader. Only after graphics parity,
   add GI/upscalers/RT (note King Kong v2.1.3 disabled DLSS/FSR3/4/XeSS due to
   performance issues; a README feature is not evidence of a stable implementation).

## What is shipped by this integration

- Source-only, exact upstream-pinned optional Hulk NativeX diagnostic:
  `ci/patch_native_x_hulk_pipeline_dependency_probe.py`.
- Static unit regression checks, idempotence, default-off semantics:
  `ci/test_patch_native_x_hulk_pipeline_dependency_probe.py`.
- One new documented setting (false) in the **existing**
  `native_port/incredible_hulk.toml`. No second TOML or second workflow.
- Existing Windows builder applies this tiny patch after the Vulkan fix chain.
  The test must pass in preflight for BOTH the legacy and 919371b5 source pins.

**Not shipped/NOT claimed:** a complete King Kong renderer integration, new EoR
LOD logic, world texture packs, offline shader pack, working DX12 backend,
DLSS, full missions, a final quality-approved remaster or user-GPU proof.

## Additional independent audits: Blue Dragon and Fable II (10 Nov 2026)

- **[re:Blue / Blue Dragon](https://github.com/zolaware/reblue)** (BSD-3-Clause):
  a real, released ReXGlue native port with a custom `src/gpu` layer. Its
  `src/gpu/surface_registry.cpp` keys and retires native targets explicitly,
  `src/gpu/draw_framebuffer.cpp` manages RT/depth barriers and read-after-write
  sampling, and `src/gpu/resolve.cpp` validates destination sample count,
  dimensions and format before copying/aliasing. It models EDRAM resolve
  semantics, so the GPU backend is **not** a drop-in replacement for Hulk's
  no-EDRAM Vulkan rendering. Reuse the safety invariants and test philosophy,
  not its game-specific targets or shader/resource formats.
  `src/vfs/prefetch.cpp` warms known asset directories on a background thread,
  with cancellation on shutdown. Hulk would need evidence-based EoR archive
  prefetch scheduling rather than copying Blue Dragon's directory list.
  Its installer and mod manager also validate user-owned disc content, preserve
  profiles and never ship copyrighted game assets.
- **[Fable2RE](https://github.com/Vynx-Velvet/Fable2RE)** (project-authored MIT,
  bundled SDK code retains other licenses): reconstructs readable C++ function
  by function, but explicitly reports an experimental port, not a finished
  full-game remake. Its independent `tools/pm4_state.py` replays captured
  Type 0/3 writes and compares PM4 register state with an XDK shadow image.
  The **concept** is useful for Hulk's observed `FETCH-WRITER` and RT0
  mismatches: obtain a safe guest PM4 snapshot and compare register state at
  the exact draw. Do not use the Fable-specific capture format, guest offsets
  or hardcoded addresses directly. Its `tools/validate_commit.py` also blocks
  game-derived assets from public commits.
- **[Fable2Remake](https://github.com/Fable2Recomp/Fable2Remake)** (MIT):
  lists 4K, 60–240 FPS and more as *planned* capabilities. It is **not**
  evidence of a completed or verified GPU renderer. Use its revision-specific
  TU1 approach as a research lead, never swap a Fable XEX or profile into Hulk.

## A repeatable Hulk-specific rendering evidence tool

`python ci/hulk_render_log_audit.py <local-log-173.log> --compare <local-candidate.log>`

The local-only report produces JSON grouped by invalid texture fetch, primitive
topology, shader analysis, `getCompTexLOD`, PM4 color/depth mismatch,
DMA rejection and sample `FETCH-WRITER` provenance. It also extracts the most
recent frame's draw and pipeline-skip counters and compares observed messages
with a later build. The cumulative `HULK/PM4-SURFACE total` is a
**high-frequency cumulative replay metric**, not a number of unique Draws.
This tool **does not fix GPU output** and cannot substitute for GPU frame
captures or actual gameplay. Do not check private user log files or generated
log reports into public Git.

Source-only regression: `python ci/test_hulk_render_log_audit.py`.

## Independent Hulk PM4 register oracle (source-only)

A standalone experimental diagnostic, inspired by Fable2RE's PM4-vs-shadow
cross-check, is in `ci/hulk_pm4_register_oracle.py` with synthetic unit tests
`ci/test_hulk_pm4_register_oracle.py`. Run locally on **user-owned** raw
big-endian PM4 command bytes:

`python ci/hulk_pm4_register_oracle.py <local-pm4-stream.bin> --output <local-report.json>`

The parser reconstructs only directly observed Type0/Type1/SET_CONSTANT
register writes and reports watched fetch/RT/viewport state at DRAW_INDX.
LOAD_ALU_CONSTANT without an authenticated guest-memory snapshot explicitly
invalidates the known value of that register. BIN-predicated state, indirect
buffers and malformed packet tails are flagged **incomplete**, never invented.
Unlike Fable2RE's game-specific capture reader, this takes plain PM4 bytes.
Nothing runs automatically in the game and no private PM4 buffers are
distributed. A further **actual game command-buffer capture** is necessary
to locate the first incorrect Hulk fetch register value.

Example register class from NativeX itself: class=1 writes fetch constants
beginning at GPU register 0x4800. A fetch writer recording
`0x44A00000` / `0x44340000` equals IEEE754 1280.0/720.0, respectively,
but the float-like bits alone do not prove the actual guest stream is wrong.

## Native-in-engine PC menu audit (12 November 2026; user re:Blue reference)

Rechecked **live source** of `zolaware/reblue` and
`portingpete/The-Darkness-Recomp` rather than copying visual UI code.
The user's desired result is a **real Hulk game menu**, not another
ImGui/F4/F10 window or launcher.

### Reference implementations and why they work

- **re:Blue / Blue Dragon** (BSD-3-Clause):
  `src/engine/menus/title_menu.cpp` hooks the original `TitleTask_Draw`,
  `TitleTask_Update` and `TitleTask_OnChildComplete` and adds the menu
  to the real title/task transition. `config_menu.cpp` creates a real
  child task (`D2AnimeTask::Load`), binds its parent/child notifications
  and owns a menu state machine. `config_layout.cpp` and
  `config_menu_data.cpp` publish data-driven UI layout through the
  original animation/menu asset path, while `config_menu_input.cpp`
  handles keyboard/mouse/controller actions in the same navigation flow.
  Settings are described once in `src/core/settings_model.h`, and
  display/input/audio hooks apply them to host systems. The game engine
  itself remains the menu owner. This is the pattern Hulk must reproduce
  using **Hulk's own EoR UI system**, not Blue Dragon's `D2Anime` classes.
- **The Darkness Recomp** (GPLv3): its `RENDERING.md` describes an
  engine-owned `Options > Video Settings` implemented through original
  `CubeMenu`/`CubeButton` and built-in compiled menu resources
  (`CubeWnd.pc.xcr`, `GameContext_Create.pc.xdf`). Both menu read paths
  are intercepted and the original save/apply path is honored. This is
  another example of extending *original* menu data instead of drawing
  an unrelated host overlay, but does not share Hulk's UI structures.

### Hulk facts established, and the missing discovery work

- The currently shipping Hulk source's `src/pc_menu.cpp` is a
  standalone `rex::ui::ImGuiDialog` opened on F10. Its FPS buttons
  modify `gpu_guest_fps_limit` and defer persistence to F4. It is
  **not** integrated into original EoR menus. Do not call it final UX.
- The **validated** USA XDK 2.0.6534 graphics hooks are listed in
  `native_port/hulk_native_x_profile.ini` (e.g. DrawVertices
  `0x8288DF30`, DrawIndexedVertices `0x8288E318`, Resolve
  `0x828897F8`, SetRenderTarget `0x8287F2C0`, Present
  `0x82880778`). These are **graphics APIs, NOT title menu entrypoints**.
  No verified Hulk title/options/pause task entry addresses appear in this
  profile. Borrowing re:Blue's addresses or task layouts would corrupt memory.
- `native_port/analysis/guest_host_mapping.csv` has address-only guest/native
  mappings (no symbol names). `native_port/analysis/pm4_candidates.csv`
  concerns GPU packet emitters. Neither establishes the menu state machine.
- The correct next reverse-engineering step is a **read-only EoR title and
  pause menu callgraph** on the pinned USA XEX: find menu string/resource
  references, constructors, button action dispatchers, state transitions,
  update/draw callbacks and parent-child/task ownership. Verify indirect
  calls with guest PPC flow and binary revision before adding hooks.

### Acceptance contract (same native port, one workflow)

1. Identify `Title → Options`, `Pause → Options` and `Back/Resume`
   transitions at real Hulk guest addresses, with callsite evidence and
   object ownership. No guessed offsets, no `REX_HOOK` on unverified PPC.
2. Extend the **existing Hulk menu hierarchy/resources** with
   Video (resolution/aspect, fullscreen/windowed, vsync, AA), Controls
   (real mouse yaw/pitch, remapping), Audio and language plus Exit.
   The gameplay can still show original story/mission settings; no modal
   host ImGui overlay and no separate launcher should be needed.
3. Separate an engine-facing settings model from host application APIs;
   validate bounds, queue graphics-mode changes safely across the render
   thread and implement atomic user-profile persistence. Do **not** enable
   `hulk_native_pc_saves` until retail PC save migration is validated.
4. Use the game's UI focus and mouse hover/hit-test semantics; keyboard
   glyphs must follow actual bindings, not synthetic XInput state. Mouse
   capture pauses while a menu is active and resumes on return to gameplay.
5. Tests: menu state-machine entry/exit, save/apply/rollback, focus/loss,
   keyboard/mouse without controller, 2560x1600 letterbox and 175% DPI;
   manually verify title, gameplay pause, save, exit and restart. Preserve
   the F10 diagnostic overlay only as an optional developer interface
   until the in-game replacement passes all contracts.

**Implementation status:** reference comparison and hook-discovery
requirements are established, but **no original EoR title/pause menu hook
has been implemented or certified** as of this audit. A successful CI
compile is not UI or rendering parity.
