# Hulk NativeX GPU crash: minidump evidence (2026-11-08)

The six-part user-provided dump (5,869,197,231 extracted bytes) is **not** checked into Git.
It was recorded at 07:57:30 Kyiv time during the NativeX-enabled log 077.

- Exception: 0xC0000005, write to 0x20, VCRUNTIME140.dll+0x1BC23.
- Faulting AVX instruction is vmovdqa to (%rcx) in a memcpy/memmove implementation.
- The return site is incredible_hulk.exe+0x2EBF0F7.
- At the caller, a Diligent-style virtual MapBuffer call returns a null mapped pointer
  and the next memcpy requests exactly 0x300 bytes from Xenos fetch constants.
- Upstream NativeXRenderer bf965e9 src/draw.cpp defines
  kFetchConstantsSize = 32 * 6 * 4 = **768 bytes**, then calls
  MappedBuffer(context, fetch_constants_buffer_) and memcpy(map.data(),...).
  The source register offset 0x12000 is 0x4800 * 4, matching
  XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0.
- Log 077 records **"Space in dynamic heap is exhausted!"** immediately before
  the exception, and no completed "renderer: frame" lines. Missing/incorrect
  guest frame-end hook remains the likely resource-reclamation defect.
- The separate log 078 uses Xenos FSI, has an image, and later hangs.
  This 07:57:30 dump **does not** diagnose that distinct 08:04 hang.

The source-only ci/patch_native_x_hulk_heap_guard.py adds null checks at
the exact pinned NativeX draw constant-buffer and overlay mapped-write sites,
skipping the failed draw/upload instead of dereferencing null. **This is
a crash containment guard, not evidence that the native GPU path, stable
frame pacing, 2x rendering, the TU1 water fix or 60 FPS physics works.**
Do not change keyboard/Raw Input paths as part of this fix.

Original dump contains process memory and is not safe for public Git history.
