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
