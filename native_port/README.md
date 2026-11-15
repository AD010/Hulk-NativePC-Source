# Hulk NativePC — Windows source tree

This directory contains the native Windows x64 build integration for the
Xbox 360 USA version of The Incredible Hulk (2008). It is a **source-only**
tree: proprietary Xbox game data is not stored in this public repository.
The port is derived from the TekRantGaming community project. Preserve the
MIT copyright notices in the repository root and this directory.

## Canonical build and delivery

- **Single GitHub Actions workflow:** `.github/workflows/hulk-free-windows.yml`
  (relative to the repository root). Runs only on manual dispatch in its
  normal state; the one-off, owner-authorized release triggers are removed
  after each run is registered. Windows x64 Release only.
- **Build on Windows:** `BUILD_WINDOWS.cmd -GameRoot "X:\\path\\to\\owned\\game"`
  from this directory. Visual Studio C++/LLVM, CMake, Ninja, Python and
  the original USA game inputs are required. Add `-NativeXLink` to build
  the native Vulkan renderer integration; it does not prove correct GPU output.
- **Private outputs:** CI consumes owner-only inputs from `AD010/hulk-cloud`.
  Tested-for-packaging, versioned ZIP assets are delivered back to its
  private release; this public repository contains no game executable.
  Previous versioned ZIP releases are retained.

## Confirmed and unconfirmed status

As of 2026-11-14, GitHub Actions **#112** completed source validation,
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

## Current continuation (2026-11-15)

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
NativeXRenderer. The official release is not an ABI-compatible drop-in
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
