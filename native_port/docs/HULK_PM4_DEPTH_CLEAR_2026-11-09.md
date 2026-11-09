# Hulk NativeX 2026-11-09 — depth/clear follow-up to #20

Physical laptop output is **2560×1600 (16:10)**; the 2× 1280×720 guest buffer is **2560×1440**, letterboxed by 80 px top and bottom.

The last #20 Windows CI compiled, and private ZIP was delivered, but static source review found a definite bitfield error: Xenos `RB_DEPTHCONTROL` bit 0 is **stencil_enable**, bit 1 is **z_enable**. Synthetic PM4 capture had accidentally required stencil enable for a depth host attachment. This fixes it to `(gpu_depth_control & 0x3u)`.

Additionally pinned NativeX `ClearColorRect` and `ClearDepthStencilRect` advanced `target->written` even when neither fast clear nor partial blit occurred, and depth clear could increment when both flags were off. This patch advances the shared content write sequence only after an actual clear. This is required for correct read-after-write ownership across the already shared 1x/4x native color/depth Vulkan host images.

Source-only patches with fail-closed pinned anchors and regression fixtures; no copyrighted input. No DX12 or Xenos host render fallback.

Inputs: user `incredible_hulk_137.log`, screenshots, full RenderDoc 1.47 `3.rdc`. Offline decode validated **10,072** Vulkan chunks: **2,024 indexed Draw**, **52 Draw**, **82 dynamic rendering begin/end**, **161 SPIR-V modules (98 VS, 61 FS, 2 CS)**, main draws 1164 full 2560×1440 scissor and 854 2048×2048 scissor. No replay Pixel History possible here, so full output correctness is NOT claimed.

The new patch does not guarantee correct shaders/fetch constants, indirect PM4 behavior, 60FPS physics, water TU1, 3× GPU scaling or Alt-Tab stability; hardware gameplay validation remains mandatory.
