# Hulk NativePC — Windows source tree

This directory contains the native Windows x64 build integration for the
Xbox 360 USA version of The Incredible Hulk (2008). It is a **source-only**
tree: proprietary Xbox game data is not stored in this public repository.
The port is derived from the TekRantGaming community project. Preserve the
MIT copyright notices in the repository root and this directory.

## Canonical build and delivery

- **Single GitHub Actions workflow:** `.github/workflows/hulk-free-windows.yml`
  (relative to the repository root). **Manual dispatch only**; source
  pushes, pull requests and schedules cannot trigger a build. There is no
  temporary one-shot push exception. Windows x64 Release only.
- **Build on Windows:** `BUILD_WINDOWS.cmd -GameRoot "X:\path\to\owned\game" -NativeXLink`
  from this directory. Visual Studio C++/LLVM, CMake/CTest (3.25+), Ninja,
  Git, Python 3.11+ and the exact SHA-pinned USA XEX are required. The
  Vulkan test build still uses transitional runtime libraries; it does
  not prove independent GPU output. Local PPC entry seeding now runs
  before SDK compilation, just as in CI.
- **Private outputs:** CI consumes owner-only inputs from `AD010/hulk-cloud`.
  Tested-for-packaging, versioned ZIP assets are delivered back to its
  private release; this public repository contains no game executable.
  Previous versioned ZIP releases are retained.

## Confirmed and unconfirmed status

Current checkpoint (2026-11-25): **#179** completed internal Windows
compilation without a release ZIP. **#180** was requested for testing all
accumulated main changes; use its actual Actions status rather than assuming
success. The complete private EoR shader bank is supplied separately when
the optional release asset is absent. See
`docs/NATIVE_PORT_REFERENCE_2026-11-25.md` for the current acceptance criteria.

Historical checkpoint: on 2026-11-14, GitHub Actions **#112** completed source validation,
Windows compilation, archive verification and versioned private delivery.
The binary's scene rendering, missing menu text, colored flashes, texture
clarity at 2x, native mouse behavior and physics above 30 FPS are **not**
proven correct by CI. No general claim of a finished remaster is implied.

Relevant current notes:
- `docs/NATIVEX_D140_MIGRATION_2026-11-13.md`
- `docs/HULK_D140_LOG201_VISUAL_PERFORMANCE_2026-11-14.md`
- `docs/HULK_FULL_NATIVE_PORT_SYSTEM_CONTRACT_2026-11-11.md`
- `docs/HULK_EU_TU1_VERIFIED_STFS_2026-11-08.md`

The European Title Update is not directly compatible with the pinned USA
XEX; never replace a full original game archive with a TU differential.
The older technical reports under `docs/` are retained as audit history,
not extra build configurations.

## Historical continuation (2026-11-15)

Latest recorded source updates: per-axis mouse controls and F10 bindings,
EoR original UI CRC32/xref evidence collection, and one-workflow source tests.
For authenticated detail and unpassed release gates see
`docs/HULK_EOR_UI_PC_X360_SOURCE_AUDIT_2026-11-14.md` (2026-11-15 addendum).
**The real `UIPage_Pause_Settings` host/guest integration has NOT been
completed.** Xbox USA camera and native 16:10, corrupted PM4 shaders,
GPU flashes/low-clarity signs, 60+FPS physics, Alt+Tab, and Windows game
compilation beyond the unsuccessful #114 are not validated. Never call
source-only tests or a successfully produced ZIP a fully fixed game.


## Original EoR shader compilation to Vulkan (source-only 2026-11-15)

Sonic Unleashed Recompiled uses XenosRecomp to produce HLSL and DXC to
compile native shaders, rather than replaying every Xenos PM4 draw at runtime.
Hulk now has two complementary source-only utilities:

1. `ci/hulk_shader_effect_bank.py` reads the owner's original **Xbox 360**
   ShaderEffects index/archive outside the public repository. Invoke with
   `--xbox-index`, `--xbox-effects`, `--private-dir` and `--extract-xenos`.
   Its private `manifest.json` records verified original shader containers,
   stage and SHA-256.
2. `ci/hulk_xenos_to_spirv.py` consumes that **private** manifest, verifies
   every source container digest, calls an **adapted** XenosRecomp for each
   source, then DXC (`vs_6_0`/`ps_6_0`) and `spirv-val`. It accepts
   `--manifest`, `--output`, `--xenosrecomp`, `--dxc`,
   `--spirv-val`, `--shader-common`. An atomic `HEORSPV1` binary is written
   only if **all** shaders pass; it can be read by the existing
   `eor_spirv_bank.cpp` with the exact original SHA identities.
   Run `python ci/test_hulk_xenos_to_spirv.py` for source-only tests.

**No original shader, decoded source, raw binary or SPIR-V output is
committed to GitHub.** The converter is not silently installed into the one
Windows Release workflow and will not produce a new game ZIP by itself.
XenosRecomp is not fully generic: for Hulk, vertex fetch, interpolator
semantics, texture LOD, binding layouts, render-target formats and the
material/draw ABI must be verified against the actual game's EoR engine.
Successful offline DXC/SPIR-V validation is **not** proof of correct gameplay
pixels, native PM4-free rendering, a complete converter or DLSS support.

Official ReXGlue **v0.11.0** was published on 2026-11-14. The current
Hulk build still depends on the separately pinned
`ElGuslml/rexglue-sdk` recomp branch for PPC guest runtime and
NativeXRenderer, SHA-pinned to `4348a344`, with three reviewed ReXGlue v0.11
PPC codegen correctness backports. Details in
`docs/HULK_REXGLUE_0_11_UPSTREAM_SYNC_2026_11_20.md`.
The official release is not an ABI-compatible drop-in
replacement. The eventual independent EoR Windows runtime must replace
those guest-runtime dependencies deliberately, subsystem by subsystem,
while preserving original FMOD/Havok/save ownership and data.


### Native Hulk registered gradients (verified Xbox ShaderEffects)

The 5,194-container original bank scan isolated exactly **two** pixel
shader programs, from **one EoR effect**, with actual
`SetTextureGradientsHorz` / `SetTextureGradientsVert` and subsequent
2D `useRegGradients` texture sampling.

`ci/patch_xenosrecomp_hulk_gradients.py` (apply AFTER the strict fetch
and computed LOD patches) preserves shader-local gradients and emits
`Texture2D.SampleGrad` instead of silently dropping gradient state.
Unexpected vertex, predicated, stale, explicit-LOD or non-2D cases fail.
The offline `HEORSPV1` compiler now requires BOTH the strict LOD and
registered-gradient Hulk adapter markers before accepting shader output.
This is original shader source adaptation, not verified Vulkan PSO binding;
the 24 dynamic-flow shaders, all EoR material descriptors, game-time
shader injections and GPU command routing still require source/runtime work.

## Historical continuation (2026-11-20)

Continuation was rebased on `274cc34` (original ShaderEffects / Xenia Edge
dump crosswalk), not the historical #108/D140 revision. The USA port remains
separate from `pc2008_mod`. Shader dump matches identify candidate source
programs; they do not establish the complete live material/Draw ownership.

- Fixed the live XDK geometry snapshot budget: repeated unchanged VB/IB
  bytes share an immutable snapshot within one Present instead of charging
  the 64 MiB budget again on every Draw. Every observation still reads the
  guest bytes; changed contents at the same object/address get a separate
  version. A failed read never returns an old version. The budget counts
  new versions, and exhausted budgets reject new data rather than recycling
  storage still referenced by previous Draws. Capture/reset/Record now share
  the collector mutex. This reduces retained copies, not the necessary
  per-Draw guest read bandwidth, and is not a measured GPU FPS improvement.
- Restored the missing XDK capture include in the AOT bridge header. Fixed
  conflicting local names and the missing pixel texture declaration/binder
  in the synthetic geometry test. Production shader-resource checks remain
  strict. The existing Windows AOT test now links its sampler and SPIR-V
  resource implementations; no workflow or configuration copy was added.
- Updated stale source-contract checks to follow the exception-safe RTV
  detach helper, actual worker snapshot name, and GPU-attempt flag forwarded
  to the real BeginScene boundary. They no longer demand a premature flag
  that would suppress the visible fallback on a clean source rejection.

Local validation: XDK capture C++ tests exercise 4096 references, in-place
buffer mutation, budget exhaustion, failed reads and old-frame lifetime.
The same test passes AddressSanitizer/UBSan with leak detection disabled
because LeakSanitizer cannot run under this host's ptrace environment.
Geometry and AOT tests also compile and pass with the source lists from the
existing workflow. All 35 EoR link checks, 9 dispatcher checks and 5 shader
dump crosswalk tests pass. These are host checks, not a Windows EXE or live
Vulkan frame validation. The AOT fixtures now supply the required original
command timeline for positive captured-Draw cases; missing timelines still
fail before BeginScene rather than being permitted just to pass a test.

Outstanding: the actual game must still produce complete per-Draw
model/surface/shader/sampler/state ownership for the independent renderer.
No color-flash, HUD, mip/LOD, physics, TU1 water or full release claim follows
from these source changes. No Actions release was launched in this step.

## Cleanup and quality gate (2026-11-17)

- Removed all eight failed-run `.hulk-release-one-shot-20261117*` trigger
  files from `main`; the only Actions workflow remains manual-only.
- The Windows builder now uses `Invoke-HulkNativeXCheckedPatch`
  for 42 atomic test-then-patch units, plus 6 special steps. One
  `reviewed_patch_plan` parser shared by CI/preflight expands them into
  the same 43 regression tests and 47 reviewed NativeX transformations.
  Interleaved shader/target ABI checks remain in place.
- The three Xenia Edge SDK fixes were consolidated from six independent
  Python files into `ci/patch_sdk_hulk_xenia_edge.py` with one
  `ci/test_patch_sdk_hulk_xenia_edge.py` (nine unchanged cases).
  The adapter is invoked in the same separate index, mip and shader stages.
  Source-level function bodies were compared against their originals;
  these refactors are **not** a verified Windows build or frame test.
- Separate `ci/patch_native_x_hulk_*.py` modules and
  `ci/test_patch_native_x_hulk_*.py` fixtures still exist on purpose: they
  enforce distinct upstream source anchors and isolate regressions. They are
  *not* 90 individual game builds. Do not merge unrelated GPU state hooks into
  one massive file or delete a module until all imports, the dispatcher,
  regression checks and pinned-source compatibility have been migrated.
- The last repeated Windows CI attempts **failed**. Build #44065859493
  passed the original TexStreams regression and stopped with a model archive
  linker error: missing implementations for original surface materials and
  native indexed meshes. Its test link command now includes those real source
  files, but has **not** been rerun. No new ZIP or visual improvement is
  claimed.
- Before any next Release: run source-only Python checks, test *all* relevant
  EoR C++ modules with `/W4 /WX` and their complete link dependencies,
  verify pinned USA XEX/private input constraints, and only then run one
  manually approved Windows Release. Successful compilation alone does not
  demonstrate correct game pixels; live gameplay/RenderDoc remains mandatory.
