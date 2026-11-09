# NativeX Vulkan Hulk log149 — PM4 ownership vs full-screen image (2026-11-09)

Source-only build on SHA-pinned USA XEX. The NVIDIA RTX5070Ti report at screen 2560x1600 shows a correctly centered 2560x1440 render image, but a 1280x720 host-pixel city fragment at its upper-left, with a solid RGB 0/255/0 remainder. This is a **missing scene coverage / render-target composition** defect, NOT a physical swapchain or 16:10/DPI issue.

Grounded log149 observations:
- `[HULK/PM4-GUARD] skipped 520000 unbound indirect draws`, while fallback later recovered 280000 separate draws.
- `[HULK/SHARED-STORAGE]` confirms the color and depth 640x360/4x guest views do share native 2560x1440 images with their 1280x720/1x siblings.
- `[HULK/SCENE-RESOLVE]` says a 640x360 4x source host2560x1440 is resolved to a 640x360 host1280x720 texture; this is expected if post-processing later properly expands it, but the observed screenshot suggests the final composition only writes a 640x360 guest region. It does **not** by itself prove a faulty copy.
- `[HULK/SCISSOR-FIX]` was triggered: merely widening the output scissor did NOT cover the whole scene. Avoid masking the issue by stretching the final presented framebuffer, which would distort UI.
- In contrast to log145, log149 has **no 'pipeline storage ... shaders from earlier runs' lines**; the persistent NativeX shader cache is disabled and is not evidence of the green area.
- There are still invalid texture-fetch constants (278 warnings), unsupported primitive topologies (50 errors), and many unbound draws. These require GPU-capture/pass correlation; a mere CI SUCCESS cannot certify visual accuracy.

**Code defect fixed:** The old `OnHulkGapSingleDraw` ran an XDK-only bound-object guard *before* `HulkPacketSurfaces`. That bypassed the packet-time GPU RT/depth history matching, even if authentic XDK surfaces had been observed earlier. The guard is removed, allowing strict real-history match first; only unmatched Draw falls to the existing XDK fallback, itself requiring a genuine bound object. `[HULK/PM4-REGISTER-ONLY]` counts newly recovered draws previously missed. No fake GuestSurface or Xenos host draw.

This is a scoped evidence-based correction, **not** confirmation of a full frame repair. Remaining GPU investigation should inspect the *first pass that changes (x=1280, y=720) of the 2560x1440 output target* and compare its attachment/viewport/scissor against pass (x=640,y=360) in RenderDoc Pixel History. Need native Vulkan hardware replay to assert cause. Do not blindly upsample the front buffer.
