# Hulk NativeX 2026-11-09 — depth/clear follow-up to #20

Physical laptop output is **2560×1600 (16:10)**; the 2× 1280×720 guest buffer is **2560×1440**, letterboxed by 80 px top and bottom.

The last #20 Windows CI compiled, and private ZIP was delivered, but static source review found a definite bitfield error: Xenos `RB_DEPTHCONTROL` bit 0 is **stencil_enable**, bit 1 is **z_enable**. Synthetic PM4 capture had accidentally required stencil enable for a depth host attachment. This fixes it to `(gpu_depth_control & 0x3u)`.

Additionally pinned NativeX `ClearColorRect` and `ClearDepthStencilRect` advanced `target->written` even when neither fast clear nor partial blit occurred, and depth clear could increment when both flags were off. This patch advances the shared content write sequence only after an actual clear. This is required for correct read-after-write ownership across the already shared 1x/4x native color/depth Vulkan host images.

Source-only patches with fail-closed pinned anchors and regression fixtures; no copyrighted input. No DX12 or Xenos host render fallback.

Inputs: user `incredible_hulk_137.log`, screenshots, full RenderDoc 1.47 `3.rdc`. Offline decode validated **10,072** Vulkan chunks: **2,024 indexed Draw**, **52 Draw**, **82 dynamic rendering begin/end**, **161 SPIR-V modules (98 VS, 61 FS, 2 CS)**, main draws 1164 full 2560×1440 scissor and 854 2048×2048 scissor. No replay Pixel History possible here, so full output correctness is NOT claimed.

The new patch does not guarantee correct shaders/fetch constants, indirect PM4 behavior, 60FPS physics, water TU1, 3× GPU scaling or Alt-Tab stability; hardware gameplay validation remains mandatory.

## 11 November 2026 — actual Vulkan attachment load-ops from 3 user RenderDoc captures

This corrects the earlier weak inference that zero vkCmdClear* **means no clears at all**.

Verified offline using user-owned RenderDoc **1.47** captures and its public serializer:
`renderdoc/serialise/{rdcfile.cpp,lz4io.cpp,serialiser.cpp}`,
`renderdoc/driver/vulkan/{vk_serialise.cpp,wrappers/vk_cmd_funcs.cpp}`.
LZ4 1 MiB chained-dictionary blocks fully decoded, then all chunks parsed
by their 64-byte alignment and documented metadata flags. Each
`VkRenderingAttachmentInfo` contains `sType=1000044001` and its
serialized `loadOp` at byte offset +33, after the optional pNext marker,
imageView, imageLayout, resolveMode, resolveImageView and resolveImageLayout.
All decoded `loadOp` and `storeOp` enum values were in range.

| Capture | Vulkan chunks | Draws | BeginRendering | Attachment `LOAD` | `CLEAR` |
|---|---:|---:|---:|---:|---:|
| `1.rdc` | 6,860 | 1,444 | 68 | 92 | 1 |
| `2.rdc` | 9,546 | 1,855 | 86 | 141 | 1 |
| `3.rdc` | 10,072 | 2,076 | 82 | 132 | 1 |

Every single `CLEAR` is **one color attachment** in a pass with
`renderArea.extent=(2560,1600)` (the laptop's output size). No
`VkRenderingAttachmentInfo.loadOp=CLEAR` occurs on any other color or
depth/stencil attachment in these three captures. All other 365
attachments have `loadOp=LOAD`; all 368 attachment storeOps are STORE.
No explicit vkCmdClearColorImage, vkCmdClearDepthStencilImage or
vkCmdClearAttachments occur in the 3 command streams. Counts:
68+86+82=236 rendering passes; 93+142+133=368 attachments;
92+141+132=365 LOAD, 3 CLEAR.

**Important non-conclusion:** this does **not** prove the game has no
3D/depth clear operation. Some passes contain `vkCmdDraw(vertexCount=3)`
or `vkCmdDrawIndexed(indexCount=6)`; an Xbox XDK `ClearF` can be
implemented as a `RECTLIST` shader draw. Color/depth content is only
proven missing after RenderDoc Pixel History or equivalent before/after
attachment replay and verified XDK PPC method recognition.

**Next exact test:** identify the genuine Clear/ClearF/Resolve function in
the SHA-pinned **USA** XEX via `ci/audit_hulk_native_xdk_ppc.py`
(`unverified_xdk_rectlist_clear_candidates`), confirm PPC call ABI and
`RB_MODECONTROL`, then check whether the native Vulkan backend emits
a correct shader clear or a host clear with accurate color/depth, including
the 640×360/4× ↔ 1280×720/1× view transitions. Never import a PAL
function address, generate fake EDRAM targets, or insert a blanket clear.

No proprietary shaders, XEX/arc bytes or RenderDoc binary assets are
embedded in this audit. This is **forensic evidence only**, not verified
visual correctness or an acceptable final release.
