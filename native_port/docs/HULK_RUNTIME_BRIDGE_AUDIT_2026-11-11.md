# Hulk NativePC — Windows runtime and wrapper audit (2026-11-11)

Scope: the USA `default.xex` port, its Windows x64 release, and comparison
with `portingpete/The-Darkness-Recomp`. This is **not** a GPU-rendering fix.

## Verified Hulk build facts

- `native_port/CMakeLists.txt` links the executable to ReXGlue, NativeXRenderer
  and DiligentCore, with Vulkan selected by `BUILD_WINDOWS.ps1`.
- `rexruntime.dll` and `rexgpu-xenos.dll` are bundled by the builder. The
  Xenos plugin remains involved for guest command processing/synchronization;
  its presence alone does **not** mean host rasterization uses Xenos.
- `native_x_renderer_copy_runtime(incredible_hulk)` supplies the NativeX
  runtime files selected by its CMake config.
- Other build-directory DLLs are copied into the test release; the
  source-free ZIP verifier permits a small set. **No CMake option in Hulk's
  own top-level CMakeLists.txt proves that every upstream component uses /MT**.
  Inspect final PE imports before declaring the build CRT-independent.
- The build input is pinned to USA XEX SHA256
  `346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0`.
  Do not substitute a PAL or devkit image.

## Darkness is not an interchangeable renderer

`portingpete/The-Darkness-Recomp` has its own D3D11 renderer, engine-scene
reconstruction and separate native runtime, audio/XMA, and input bridges.
Its `tools/package_release.py` explicitly includes `msvcp140.dll`,
`vcruntime140.dll`, `vcruntime140_1.dll`, `avcodec-darkxma-62.dll`,
`avutil-darkxma-60.dll` and `libwinpthread-1.dll`. Those files exist to
satisfy **that build's** dependencies. They do not repair Hulk's PM4
register ordering, Xenos shader translation, corrupt texture-fetch descriptors,
resolve aliasing or RenderTarget/depth state.

## Runtime policy

- On the user's current Windows installation, recent supported VC++ runtimes
  are already installed. No copying of Darkness runtime DLLs is justified.
- Check both immediate and delay-load PE imports of **every shipped EXE and
  DLL**, not just `incredible_hulk.exe`.
- `ci/audit_hulk_runtime_imports.py` calls MSVC `dumpbin /dependents`; output
  contains DLL names only. It distinguishes app-local dependencies, Windows
  system imports, VC++ Redist imports, Vulkan loader and other missing DLLs.
  Its strict mode is an optional release gate; ZIP verification prints the
  nonblocking summary.
- Vulkan loader `vulkan-1.dll` and the Vulkan implementation must be correctly
  installed with the GPU driver; bundling a random graphics wrapper is unsafe.
- Do **not** ship Microsoft runtime DLLs, vendor libraries, or any extra shim
  without a real binary import (or documented dynamic LoadLibrary usage), a
  compatible build, license, and game-level test.
- A missing DLL is a startup/deployment problem, while #56's faulty PM4
  primitives, 1D texture descriptors and 27-dword vertex-shader faults happen
  after the renderer has started. Runtime packaging is **not a cure**.

## Next validation

1. Wait for GitHub Actions #57 to finish; compile success is not pixel proof.
2. Capture the import audit from the next CI package validation and investigate
   only genuine unknown DLL dependencies; preserve release compatibility.
3. Fix GPU PM4/texture/shader/render-target problems in their respective native
   renderer code, and compare before/after game frame captures.
4. Consider a game-specific wrapper/hook only if a traced Windows import, a
   missing XDK entrypoint, or an engine native-call contract justifies it.

References:
- Microsoft, Deployment in Visual C++:
  https://learn.microsoft.com/en-us/cpp/windows/deployment-in-visual-cpp
- Microsoft, /MD, /MT, /LD:
  https://learn.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library
- Khronos, checking for Vulkan support:
  https://docs.vulkan.org/guide/latest/checking_for_support.html
- PortingPete, The Darkness Recomp:
  https://github.com/portingpete/The-Darkness-Recomp
