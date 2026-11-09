# Hulk NativePC image ownership audit — November 9, 2026

The native Vulkan renderer previously marked **all five bound color/depth targets as written before calling DrawPath::Draw**. That call silently skipped draws when their shader/pipeline was compiling or rasterization was disabled. It also ignored the Xenos color mask and depth write bits. With shared 1x/4x Hulk views, this wrong `written` ordering may select an unmodified target during later Resolve. This is a source-level defect and the reason for this patch; it is **not yet proven** to be the single root cause of screenshot149's large lime-green 3/4 frame.

The patch changes `DrawPath::Draw` from `void` to `bool` (whether DrawInternal submitted a rasterizing draw); it then increments `written` only for images with active color write-mask bits or writable depth/stencil, and only after success. Other host-renderer paths are untouched. Separate `ClearColorRect/ClearDepthStencilRect` was already corrected in prior commits.

It is safe to compile as part of the existing single Windows workflow. The gameplay still requires **RTX 5070 Ti hardware verification** with output 2560x1440 letterboxed in a physical 2560x1600 swapchain. The source work cannot assert full rendering parity or elimination of the green frame without hardware replay/RenderDoc.
