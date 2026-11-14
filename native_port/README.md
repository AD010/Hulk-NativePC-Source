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
