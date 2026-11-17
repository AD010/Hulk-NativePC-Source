# Hulk NativePC / original PC mod — source-only engineering contract

This file defines project organization and acceptance criteria for coding
agents. It is not a claim that the native port is complete.

## Two independent workstreams — never mix them

1. `native_port/`: original **Xbox 360 USA** The Incredible Hulk (2008)
   recompiled into Windows x64. Target: independent, native Edge of Reality
   Windows runtime and original game renderer rewritten for Vulkan, with
   authentic shader programs, materials, textures and host render targets.
   **No Xenia, Xenos PM4 replay, ReXGlue or NativeX renderer is intended
   to remain in the FINAL game rendering path.** The pinned
   `ElGuslml/rexglue-sdk` + `NativeXRenderer` are *temporary compatibility
   and reverse-engineering infrastructure*, not the finished port.
2. `pc2008_mod/`: separate mod for the existing **original Windows 32-bit
   Direct3D 9 game**. Build x86, preserve its real D3D9/DXVK input and
   assets, keep reversible INI/install/rollback. Never treat Xbox XEX
   callsites or texture formats as original PC source evidence.

Work only inside the existing `AD010/Hulk-NativePC-Source` branch `main`
unless the owner explicitly requests another branch or repository. The
owner's actual USA XEX, game assets, title updates, saves and copyrighted
outputs belong **only in private local/runner storage**. Never publish game
binaries, GPU captures containing game assets, generated guest PPC source,
private original SPIR-V/shader bytes, private logs, secrets or token values.
Preserve upstream licenses and notices.

## Actual source layout (no unnecessary new repositories)

- `native_port/src/eor_*`: developing independent original Edge of Reality
  assets, meshes, shader-bank, material/sampler and frame renderer.
- `native_port/src/launcher/`: Windows integration, input, swapchain bridge.
- `native_port/BUILD_WINDOWS.ps1`: one local Windows build entrypoint.
- `native_port/analysis/` and `native_port/docs/`: source-only verified
  research, not a runtime patch oracle.
- `pc2008_mod/`: wholly separate original PC mod and profiles.
- `ci/`: verified source transformations, their regressions and audits.
- `tools/ghidra/`: optional local reverse-engineering helpers (no assets).
- `.github/workflows/hulk-free-windows.yml`: the **only** GitHub Actions
  workflow, manual `workflow_dispatch` ONLY. No automatic push, PR,
  schedule, temporary marker, second workflow or speculative release.

The 47 pinned NativeX compatibility transformations and 43 associated
regressions share a canonical staged build plan:
`Invoke-HulkNativeXCheckedPatch` in `BUILD_WINDOWS.ps1` combines 42
independent test-then-patch pairs; six special stages are explicitly
separate because they do not share the same dependency boundaries.
`ci/native_x_d140_dispatch.py::reviewed_patch_plan` is the only Python
reader of the expanded source plan; use it from CI and preflight tests.
The three Xenia Edge ReXGlue fixes (index pool, packed mip, scalar shader)
are grouped in `ci/patch_sdk_hulk_xenia_edge.py` with one suite
`ci/test_patch_sdk_hulk_xenia_edge.py`, still applied at the correct
different dependency stages. Do not resurrect obsolete per-fix scripts.

## Mandatory quality gates BEFORE a Windows Release

1. Source-only security: no game bytes, private archives, compiled games,
   cache files, credentials, copied SDK/guest code or one-shot markers.
2. Source-only Python tests and one canonical patch-order check must pass,
   including backward-compatible ReXGlue/NativeX pinned-SHA checks and the
   standalone MSVC source-unit link dependency inventory. Avoid tests that
   merely accept their own hard-coded expected strings.
3. Real C++ modules compile and link with all dependent sources; use strict
   MSVC `/W4 /WX` where supported. The last attempted 2026-11-17 Release
   **failed** on missing link inputs. The source fix is not yet Windows
   compiled. Stop and correct errors LOCALLY before requesting any release.
4. Only when all independent tests pass and the owner approves a final
   evaluation build may the one manual Windows Release be dispatched.
   Return success only with its real build SHA, complete passed job,
   artifact SHA, and a verified private archive. Never invent ZIP links.
5. RenderDoc and actual gameplay on Windows must verify image correctness:
   texture mip clarity, missing HUD/menu text, no red/yellow flashes,
   scene Resolve/depth/alpha, 2560×1600 16:10 presentation, 175% DPI,
   native mouse/keyboard, Alt+Tab, city LOD/pop-in, physics at 60+ FPS,
   save format and menu/audio stability. C++ tests/CI alone never prove
   better pixels. **Do not call the project a remaster until these pass.**

Do not start a series of speculative GitHub Actions runs. A failed build
should generate a concrete fix and a source-only regression; no new Release
until the complete source gate is green. Never bypass failed tests by
relaxing the source-only audit or suppressing compiler warnings.

## Engine ABI and rendering evidence — do not invent facts

- The real USA `default.xex` and exact verified PPC callsites are distinct
  from PAL/EU title updates, PC `HULK.EXE`, synthetic test callsites or
  guest wrappers. Never merge their addressing.
- Original XDK draw wrapper `r3..r10` snapshots or PM4 dumps alone do NOT
  prove EoR mesh, technique, material, shader identity, texture SRV,
  World/ViewProj camera ABI or original engine draw owner.
- Source-traced `Models.IND/ARC`, `Textures.ARC/TexStreams.ARC`,
  `ShaderEffects` AOT banks and original live game-frame evidence must
  agree before independent Vulkan PSO/Draw and 16:10 camera can render.
  No made-up geometry or texture bytes and no PM4 compatibility fallback
  falsely advertised as a successful new engine renderer.
- Use ReXGlue, Xenia Edge, XBE Recomp, Sonic Unleashed Recompiled and
  Zelda64Recomp as **architectural references**; don't copy protected
  source or assume their GPU/physics structure matches Hulk.
- Original PC mod's `ProtectOriginalHUD` safety can disable resolution/FOV
  overrides. Test real D3D9/HUD presentation before claiming any INI
  profile works as intended. No fake DLL or game executable in public Git.

## Communication

Record concrete changed source paths and tests actually run. Report
compile success separately from GPU acceptance. Never imply unattended
work when it is not actively scheduled. The owner wants a properly tested
visual ZIP, not frequent intermediate changes or speculative claims.
