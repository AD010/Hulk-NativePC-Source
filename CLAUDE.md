# Claude / GitHub coding-agent instructions — Hulk NativePC

## Project and non-negotiable boundaries

This repository is the **source-only public workspace** for a native Windows
recompilation of **The Incredible Hulk (2008), Xbox 360 edition**. The goal is
native Vulkan rendering via NativeXRenderer + ReXGlue, not another Xenia wrapper.

- **NEVER** introduce original Xbox 360 `default.xex`, Title Updates/XEXP,
  `*.arc`/`*.ind`, copyrighted game assets, decrypted PowerPC output,
  generated guest code, private logs containing such material, keys or tokens
  into this public repository, PRs, issues, comments, or public Actions logs.
- The private `AD010/hulk-cloud` repository contains owner-only inputs and
  releases. **Do not fetch, mirror, inspect, or disclose its contents.**
  Do not ask for or use `HULK_PRIVATE_REPO_TOKEN` or other private CI secrets.
- Respect existing upstream and project licenses; do not paste third-party
  copyrighted implementations without the necessary rights and notices.
- Preserve the **single existing** `.github/workflows/hulk-free-windows.yml`
  and its Release-only Windows build. Do not introduce extra workflows, public
  game-binary artifacts, unpinned download steps, or automatic private builds
  from unreviewed PRs. Focus on source-only changes and tests.
- Propose changes through **small, reviewable pull requests**. Do not merge,
  release, or claim graphical correctness without human review and a game
  hardware test.

## What needs engineering work

1. **Correct Xbox 360 graphics**: guest PM4 chronological state, packet-time
   color/depth surfaces, RT0 EDRAM identity, index-buffer snapshot lifetime,
   PrimitiveProcessor topology, shadow/depth writes, texture fetch registers,
   1x/4x Resolve and Present. NativeX must reproduce the correct game image,
   not mask missing draws with artificial geometry or fabricated textures.
2. **Shader accuracy**: Xenos shader microcode, `IM_LOAD_IMMEDIATE`, Vulkan
   SPIR-V translation, texture sampling/LOD and shader constants. Preserve
   original shader semantics rather than replacing errors with arbitrary
   zeros. Narrowly validate each fix against real source and test cases.
3. **Native resolution**: real 2x+ internal render scale without pixelation
   or broken post-processing. The owner's 2560x1600, 16:10, 175% Windows DPI
   desktop needs correct 2560x1440 game content with 80px top/bottom bars,
   DPI-aware UI, mouse and cursor behavior.
4. **World streaming and skyline**: distinguish genuine GPU faults from
   EoR Engine's original `ZoneLowLod`, `HiResDataset`, `ZoneStreamManager`,
   `CameraFarPlane`, `FogNear`/`FogFar` behavior. Improve skyline, pop-in,
   transitions and texture availability only after reverse engineering real
   zone data and call sites. Never claim known schema field names imply known
   runtime values or a hidden complete high-detail city.
5. **PC polish**: raw/native mouse camera and keyboard, optional VSync,
   unlocked FPS with physics timing corrections, Alt+Tab stability, settings
   menu, native fullscreen, night scenes and all mission effects.
6. **Title Update**: a European TU1 is not directly compatible with the
   pinned USA XEX. Do not apply incompatible XEXP blindly.

## Evidence and implementation requirements

- Baseline owner report: run #51 compiled but rendered black sky, distorted
  geometry, harsh outline/flicker and dark menu. Successful CI compilation
  is **not** a visual success.
- Log `incredible_hulk_173.log` (owner-private, not committed) included
  `1D texture is too wide (5891328)`, tiled 1D texture fetch warnings,
  unsupported primitive types, shader analysis access violations on
  `IM_LOAD_IMMEDIATE`, and RT0 packet-vs-XDK surface base mismatches.
  Do not silence these findings or invent missing DMA index data.
- First read existing `ci/patch_native_x_hulk_*.py`, adjacent
  `ci/test_patch_native_x_hulk_*.py`,
  `ci/test_native_x_patch_chain.py`, and
  `native_port/BUILD_WINDOWS.ps1` to understand the **actual patch order**.
  Many patches require exact pinned NativeX source and fail safely when
  anchors drift. Maintain idempotence and both pinned upstream variants
  where already supported.
- Prefer one verified root-cause fix over speculative blanket overrides.
  Each fix needs a reproducible source-level test, evidence of the failing
  code path, regression protection, and clear risks.
- Run focused Python unittest files and `python ci/test_native_x_patch_chain.py`
  when network/upstream sources are available. Where a command requires the
  private game input or a Windows GPU, explicitly report it as untested.
- A PR must state: observed problem, suspected root cause vs confirmed
  evidence, exact files modified, tests actually run, source-only security
  check, expected owner-visible changes, and remaining hardware checks.
- Do not introduce fake high-res texture content, new console assets,
  guessed PPC addresses, or imaginary test results.

## Collaboration

Use GitHub issues and pull requests for narrowly scoped work. Keep the
main builder untouched unless an explicit integration change is essential
and reviewed. The owner tests actual game scenes, especially rooftops,
Manhattan skyline, night missions, water, fast traversal, graphics scaling,
and controls, after a build has been proven to contain the submitted code.
