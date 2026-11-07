# Hulk Native PC — active Windows source tree

This source is based on the community head-start project by TekRantGaming.
Retain the accompanying LICENSE and upstream copyright notices.

The current build is a standalone incredible_hulk.exe with its runtime DLLs
and one incredible_hulk.toml. No separate TRG launcher is built or required.

Canonical build:
- In GitHub: .github/workflows/hulk-vulkan-build.yml (from repository root).
- Locally on a Windows development machine: BUILD_WINDOWS.cmd with a valid
  -GameRoot argument pointing to a locally owned extracted game folder.
- For the NativeXRenderer library bridge: BUILD_WINDOWS.cmd -NativeXLink,
  which does not imply working native GPU rendering by itself.

Successful CI: 42388026994 (2026-11-07) compiled and packaged a Windows
game. Do not confuse compilation with validation of scene rendering.

Important unresolved requirements:
- XDK 2.0.6534 GPU function signatures and true DeviceLayout/ObjectLayout;
  Xenos remains the currently verified rendering fallback.
- Official Xbox 360 TU1: original user update verified, but its runtime/
  codegen application and differential ARC/IND overlay are not integrated.
- Direct camera input: pointer and yaw/pitch offsets not yet verified.
- Water behavior, 2x/3x resolution, color parity, FPS/physics and controls
  still require testing in a Windows gameplay run.

Read native_port/docs/HULK_TU1_XDK_AUDIT_2026-11-07.md,
native_port/docs/HULK_XDK_GATE_2026-11-07.md and
native_port/docs/HULK_NATIVE_GPU.md for the technical status.
Do not overwrite full original game archives with TU1 differential files.
