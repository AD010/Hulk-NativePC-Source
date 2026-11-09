# Hulk NativeX: PM4 surface ownership after capture 3.rdc (2026-11-09)

This is a source-only corrective build, not a claim of final rendering parity.

- Device/swapchain: Lenovo RTX 5070 Ti, **2560x1600 (16:10)**. The game's 1280x720 guest image scaled 2x to 2560x1440 is letterboxed with 80 physical pixels top/bottom; that is expected and distinct from the monitor's dimensions.
- User screenshots and RenderDoc 1.47 capture `3.rdc`: partial city/HUD, green-yellow corruption starting around the half-image boundary. The RDC preview was extracted but GPU Pixel History is not available here.
- `incredible_hulk_137.log`: NativeX renderer active, Xenos host drawing off, 1680 presents, thousands of synthetic PM4 draws, invalid texture fetch constants, two shader translator failures and primitive type errors; shutdown lacks a GPU crash stack.
- **Source-level issue**: synthetic `OnHulkGapSingleDraw` previously called `CaptureDraw`, which read `ReadBoundSurface(base, device, slot)` from the post-gap XDK device shadow. Multiple packet Draws in a gap can have different GPU register bindings.
- **Mitigation**: read packet-time GPU `RB_SURFACE_INFO` and `RB_COLOR/DEPTH_INFO` from the tracker's big-endian image, respect enabled color/depth slots, match a real XDK-observed surface object by strict pitch/MSAA/base/format key, and fail closed when writable targets are unresolved. No fabricated Xbox graphics resources, no Xenos/EDRAM host path.
- Underlying PixelHistory, indirect helper correctness, shader/fetch correctness, 60 FPS physics, TU1 compatibility, and runtime hang remain open release gates. A passed Windows Actions job only proves compilation.
- The uploaded `534507F1(2).pipelines` cache parses as XNPS v2 (353 shader, 595 translation, 456 pipeline records), unlike the runtime log's older 394/796/724 cache. Do not treat that cache as the failing shader's definitive binary.
