# Original EoR model data — read-only native decoder, 2026-11-15

Source-only native C++20 `eor_model_resource.cpp` inspects actual indexed
original Xbox/PC model records without ReXGlue, Xenos, PM4 or guessed
GPU buffer ownership. Test program compiled with clang++ -std=c++20
-Wall -Wextra -Werror and run successfully.

**Owner's actual original data were exhaustively checked:**
- Xbox360 USA: 4,174 indexed Models records in MODELS.ARC; 4,174 pass.
  Model kind 3 aggregates: 17. Records naming a first surface: 3,831.
- PC 2008: 1,823 indexed Models records in MODELS(1).ARC; 1,823 pass.
  Model kind 3 aggregates: 8. Records naming a first surface: 1,560.
- The original binary model record starts with 00 01 (Xbox BE) or 01 00
  (PC LE), four reserved zero bytes and an ASCII NUL-terminated model
  name; the next byte is model kind 0 or 3; a 32-bit platform-endian
  surface record count follows; where nonzero, the first surface name
  is a NUL-terminated ASCII string.
- The surface label may be named blinn*, Storefront_*, ground*, etc.
  **It is NOT proof of a particular VS/PS ShaderEffect or Vulkan pipeline**.
- Level aggregates are large: original Xbox frontend model is ~17 MB;
  the decoder enforces a 32-MiB resource bound.

This is a concrete original EoR resource inspection subsystem that can be
called immediately after the already implemented `eor::ReadResource` for
the Models group. It does NOT yet decode vertex streams, index buffers,
skinning, material-table offsets, texture descriptors, render targets,
shader constants or live game Draw calls.

**Toy Story 3 Complete Edition technical distinction:** Digital Eclipse
has publicly confirmed the 2026 native port was made from the ORIGINAL
2010 source repository. No equivalent original EoR engine source code
has been authenticated for Hulk. Therefore preserving the existing
recompiled PPC gameplay code while progressively replacing the render
path with native EoR resources is the only evidence-based main direction.
Do not fabricate a fully working Vulkan port or promise missing APIs are
implemented merely because a host C++ parser passes.
