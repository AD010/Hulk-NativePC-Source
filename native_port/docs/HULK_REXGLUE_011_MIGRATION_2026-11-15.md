# Hulk USA / ReXGlue 0.11.0 migration assessment — 2026-11-15

## Verified upstream, not a drop-in DLL update

Official `rexglue/rexglue-sdk` **v0.11.0** is a STABLE release
published 2026-11-14. GitHub compare `v0.10.1...v0.11.0` reports
**334 commits**. The current Hulk build instead pins
`ElGuslml/rexglue-sdk` `recomp` for NativeXRenderer and generated
PowerPC x64 source. DO NOT assume official v0.11 is a replacement for
the fork's compiled `rexruntime.dll` or source-level renderer hooks.

Upstream documentation:
- https://github.com/rexglue/rexglue-sdk/blob/v0.11.0/docs/site/pages/reference/cmake.md
- https://github.com/rexglue/rexglue-sdk/blob/v0.11.0/docs/site/pages/getting-started.md
- https://github.com/rexglue/rexglue-sdk/blob/v0.11.0/docs/site/pages/devices/graphics.md
- https://github.com/rexglue/rexglue-sdk/compare/v0.10.1...v0.11.0

## Concrete incompatibilities in existing Hulk source

| Current main | Official 0.11.x API | Consequence |
|---|---|---|
| `include(generated/rexglue.cmake)` | `find_package(rexglue 0.11 CONFIG REQUIRED)` then `rexglue_project(incredible_hulk)` | New SDK explicitly rejects projects that retain the old generated/rexglue.cmake |
| `rexglue_setup_target(incredible_hulk GPU_PLUGINS xenos)` | `rexglue_setup_target(incredible_hulk [NO_COPY] [EXCLUDE ...])` | Old `GPU_PLUGINS` parameter is not in documented API |
| Build-time codegen refreshes `generated/rexglue.cmake` | Manifest `incredible_hulk_manifest.toml`, `rexglue codegen` writes `generated/default/sources.cmake`, invoked automatically | AOT partition, custom Python patches, function map and hook signatures must be rebased; cannot just change a URL |
| Fork's NativeX D140 integrates with `rex::graphics` state/shader translator | SDK 0.11 uses restructured graphics modules and interface versions | Compile-time and runtime interface negotiation must be reviewed; unported NativeX cannot link blindly |
| `rexgpu-xenos` from fork plus NativeX | Official `TitleApp` requires `GraphicsFamily::kXenos`; official SDK still processes PM4 Xenos commands | Official 0.11 **does not itself remove GPU emulation**. For independent EoR Vulkan a host renderer must own real EoR Draw calls, materials and GPU fences. |
| Guest/host XAM save adapter and Win32 input hooks | Official 0.11 runtime/file-system/driver lifetime refactored | Migrate only with ABI proof, save regression and input/focus tests, and data backups. |

## Safe in-place migration order (no new repository/workflow)

1. Preserve current `main`, pinned release workflow, USA XEX SHA and
   private data; never overwrite the working source. Do not publish user
   content or guest compiled code.
2. Update PPC codegen manifests and build API in a temporary **local**
   source working directory, explicitly using `rexglue_project` and
   `rexglue_setup_target`. Confirm the exact XEX SHA
   `346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0`
   and all guest entrypoints before interpreting generated addresses.
3. Rebase the ten USA XDK hooks and `PPCContext` signatures on the new
   generated source, verifying each wrapper and state ownership without
   carrying unreviewed older fork offsets.
4. Reconcile NativeX (if still used as a temporary fallback) separately.
   GPU independence requires deleting ALL dependency on the guest Xenos
   PM4 and guest render-target path from final rendering, not renaming DLLs.
5. Preserve our independent EoR resource parsers, 5194-container private
   shader bank workflow, fixed-step clock, native camera math, native
   temporal contracts and official PC-save validation during API migration.
6. Only after a complete host integration, exercise Win32 save/load,
   per-axis mouse, audio, Alt+Tab, menu, 2560x1600, full Havok physics,
   and GPU RenderDoc reference scenes. No final release before this.

## Decision

**Do not upgrade the sole Windows Release workflow now.** A full 0.11
migration is a separate source compatibility milestone, *not* evidence
that the native EoR raster, original UI, physics, PC-save conversion
or DLSS/FSR has been implemented. The current main source remains
buildable against the previously pinned SDK until those contracts pass.
