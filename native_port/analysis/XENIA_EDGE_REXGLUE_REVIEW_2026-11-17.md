# Hulk NativePC: upstream Xenia Edge / ReXGlue source review (2026-11-17)

Scope: real public source diffs, not game-frame evidence. This report contains
no Hulk Xbox/PC binaries, shaders, textures, addresses, or proprietary data.
Our main project remains `AD010/Hulk-NativePC-Source`; only one manual
Windows Release workflow and the private `AD010/hulk-cloud` asset delivery.

## Reviewed upstream

- Xenia Edge `has207/xenia-edge`: observed main `d521a9028f16`
  (2026-11-17). The latest *GPU* entries in the retrieved path history were
  2026-11-11 display front-buffer format and crop changes, but several
  October fixes are more relevant to Hulk's shader and texture artifacts.
- Original ReXGlue `rexglue/rexglue-sdk`: observed main
  `ebac1cf0d04b` / v0.11.0 (2026-11-14). Do NOT hot-upgrade the
  independent `ElGuslml/rexglue-sdk` API by replacing it.
- Our selected ReXGlue `ElGuslml/rexglue-sdk`, branch `recomp`:
  observed head `4348a344eb86` (2026-11-14). It still contains the
  three bugs backported below. Hulk's BUILD_WINDOWS.ps1 clones the named
  `recomp` branch, reuses an existing SDK checkout without updating, and
  applies source-verified SDK patches BEFORE the expensive SDK CMake build.
- NativeXRenderer `ElGuslml/NativeXRenderer`: observed main D140
  `fa19ea85fec5` (2026-11-11); the Hulk Release pins that SHA.

## Confirmed source-level upstream findings and targeted backports

### NVIDIA invalid shader input: Xenia Edge 09d405f (2026-10-24)

Source: https://github.com/has207/xenia-edge/commit/09d405f024ee3d2c64b899178512eef69b4d05f4

The Xbox 360 32-bit **single-component** normalized vertex fetch
produces an SPIR-V scalar. In the selected ReXGlue `recomp` branch,
`src/graphics/pipeline/shader/spirv_translator_fetch.cpp` still emits
`OpVectorTimesScalar` for all three normalized scaling cases and for a
scalar result's exponent adjustment. Xenia Edge changed those cases to
`OpFMul` and described an NVIDIA shader compiler crash from the invalid
vector operation. This is a **credible class match**, not proof that
Hulk's specific previous nvgpucomp64.dll exception has this same root cause.

Hulk backport: `ci/patch_sdk_hulk_xenia_edge.py spirv-scalar SDK_ROOT`, covered by
`ci/test_patch_sdk_hulk_xenia_edge.py`; SDK shader translator version
14 -> 15 so cached invalid bytecode cannot be reused after the repair.
Run this AFTER the existing fetch-constant word3 (12 -> 13) and
computed LOD (13 -> 14) patches.

### Converted GPU index pages allocated ~16x too large: Xenia Edge 4ad875 (2026-10-26)

Source: https://github.com/has207/xenia-edge/commit/4ad875acb557910b42d39749787bc55a4b75d6e7

The actual selected ReXGlue `primitive_processor.h` still computes
`sizeof(uint32_t) * (UINT16_MAX - 2) * 3 * +SIMD_SIZE`.
The alignment padding is incorrectly multiplied by the maximum index
conversion bytes instead of ADDED. This affects GPU index pool resource
allocations, not original index values or shader output.

Hulk backport: `ci/patch_sdk_hulk_xenia_edge.py index-pool SDK_ROOT`, covered by
`ci/test_patch_sdk_hulk_xenia_edge.py`; follows the existing CPU index
cache invalidation patch. No new engine hook or emulated GPU path.

### Packed mip0 and 2x1 texture tail: Xenia Edge 344fca7 (2026-10-23)

Source: https://github.com/has207/xenia-edge/commit/344fca75880f96e6629e98323984f5d2ebd26640

The actual selected ReXGlue `src/graphics/pipeline/texture/util.cpp`
still uses `mip_address` when `packed_mips` specifies the *whole* image
including level0 is packed at `base_address`, and still categorizes the
2x1 texture as wider rather than using the square packed layout.
These are proven upstream Xenos source-address/tiling bugs.

Hulk backport: `ci/patch_sdk_hulk_xenia_edge.py packed-mip SDK_ROOT`, covered by
`ci/test_patch_sdk_hulk_xenia_edge.py`; source-aware and fail-closed.
This fixes guest fetch/source layout only. The final independent EoR
renderer must *also* prove the genuine LFXT/TexStreams source byte ranges,
Xenos subformat/sRGB, shader sampler view, complete high-detail mips,
and live original per-surface associations. Do not claim that the signs
are sharper until a new game frame proves the correct sampling.

The single staged SDK adapter and consolidated regression suite are wired in the existing `native_port/BUILD_WINDOWS.ps1`;
the existing single manual `.github/workflows/hulk-free-windows.yml`
runs their synthetic regression tests before the Release build.
**Source integration is not evidence that a Windows build/game passes.**

## Additional candidate leads — NOT blindly ported

- Xenia Edge `043a7cb4` (2026-10-25): Xenos vertex arithmetic uses
  round-toward-zero / unfused products, with demonstrated Z-fighting and
  geometry differences:
  https://github.com/has207/xenia-edge/commit/043a7cb4032e9198265195b62b0cb7c4b45bb047
  The independent EoR AOT shader translator needs a verified instruction
  semantic parity test against authentic originals. Do not globally
  alter Diligent Vulkan float controls without a supported device/driver
  feature and matching original ALU stage.
- Xenia Edge `fb963817` (2026-10-28): correctly handle render targets
  moving within EDRAM, a hypothesis for legacy Vulkan flashes.
  https://github.com/has207/xenia-edge/commit/fb9638172278
  This should *not* be reproduced as emulated EDRAM in the final host-native
  renderer; the final host engine must own physical color/depth RT state.
- Xenia Edge `990bfd85` (2026-10-28): shader interpreter loop stack
  overflow on wrong `aL`. Test if affected Hulk shader hashes occur.
  https://github.com/has207/xenia-edge/commit/990bfd85948a
- Xenia Edge `6274640`, `60c76b3`, `8984c68` (2026-11-16): XEX
  title update image page-digest checking, best-fitting installed TU and
  fail-closed fallback to base title. Hulk's available EU TU1 cannot
  be applied to its SHA-pinned USA XEX without independently verified
  USA correspondences and PPC/engine semantics.
  https://github.com/has207/xenia-edge/commit/6274640ecca2312b36a7e5b2dd2cec0436622c7c
- ReXGlue pinned `recomp` `899b78c1` (2026-11-12):
  `SetSignalGateTagged` marks the submission responsible for a guest fence,
  so renderer waits no longer block unnecessarily on subsequent guest work.
  This is useful for **transition compatibility** only; no need to
  reproduce a guest PM4 ring in the final independent EoR Vulkan path.
  https://github.com/ElGuslml/rexglue-sdk/commit/899b78c160fcd91d69adf2092daad22e6fb53238
- ReXGlue pinned `recomp` `bb609c09` (2026-11-08): dynamic SPIR-V
  resolution-scale uniforms instead of per-scale shader recompiles.
  This is not by itself a true 2560x1600-native EoR camera/RT solution.
  https://github.com/ElGuslml/rexglue-sdk/commit/bb609c091406c012eb8ea543af0274cc79e32f4
- ReXGlue original main v0.11.0 introduced PPC `vcmpbfp` NaN bounds,
  `srad/sraw` carry and `fctid/fctidz` saturation fixes (2026-11-14).
  Useful for guest CPU/physics semantic correctness, but different SDK
  interfaces and no confirmed Hulk affected PPC callsites yet:
  https://github.com/rexglue/rexglue-sdk/commit/ab61f2e693f45a7dd4bdf62c898cb659540e8fc6

## What is **still missing** for a complete native PC port

1. Source-proven *actual EoR* engine draw arguments, material/surface/technique
   ownership, vertex layout, and transform/constant uploads at the USA game's
   verified runtime callsites; XDK raw r3..r10 snapshots cannot prove these.
2. Fully complete source-owned original ShaderEffects VS/PS + validated AOT
   shader bank, exact SPIR-V resource names/types and per-draw source texture
   mappings. A pass-ordinal pair candidate is not actual engine pass proof.
3. Real game/host swapchain bridge for `DiligentEorDirectBackend` so all
   3D/UI output, camera, depth/resolve, transparencies and HUD passes execute
   independently from Xenia/PM4, including 2560x1600 (16:10).
4. Verified EoR camera World/ViewProj ABI and native input target; verified
   original Havok update call, uncapped render clock, stable mission physics;
   LOD/streaming and official menu integration; native audio and save QA.
5. Windows Release build and **real playthrough / RenderDoc GPU acceptance**
   on the owner's RTX 5070 Ti Laptop. Static tests alone do not prove visual
   results, elimination of flashes, sign sharpness, GPU load or stability.

No Release was automatically started by this source review. Keep one manual
Release workflow and deliver original-input-bearing builds privately only.
