# Hulk NativeX log146: lost native Vulkan scene after strict PM4 surface guard (2026-11-09)

RTX 5070 Ti laptop, physical **2560x1600 (16:10)**; native 2x guest buffer 2560x1440 is letterboxed.

**Evidence from user local logs:** 146 log: 0 saved shader/pipeline cache objects at first draw, frame4920 completed without observed NVIDIA compiler crash. Around 850k replayed PM4 draw packets, at least 250k explicitly rejected by new `HULK/PM4-SURFACE` strict object match; additional zero-pitch early returns not counted. 137 log (before guard) logged 12 full-output scissor expansions and a partially visible city; 146 logged zero scissor expansions and showed only UI over green/black. The exact causes of surface-key mismatches are not yet recorded.

**This patch is a regression recovery, NOT a full native graphics fix.** Known exact packet-time RT/depth matches still use the strict mapped images. When those can't be found, a bounded fallback captures the packet using real bound XDK surface objects through NativeX Vulkan, matching the path that displayed scene content before the regression. No Xenos host rendering / DX12. Controlled by `native_x_renderer_hulk_surface_fallback` (true for this verification).

For recent NVIDIA driver 621.14 early crash in `nvgpucomp64.dll`, runtime TOML now disables bulk persistent pipeline cache compilation (`native_x_renderer_pipeline_storage=false`), but still translates required shaders on demand. Whether crash is solely due to cache has NOT been proven.

The shipped TOML uses only active assignments (no comments). Source-only `hulk_native_x_profile.ini` is still required to generate/verify the user's XDK 6534 hooks during CI, but is **not** shipped in the EXE+DLL+TOML game ZIP.

Outstanding: RenderDoc PixelHistory color/depth ownership, green fragments/fetch constants, shader translation, physics60, RawInput yaw/pitch, USA-compatible TU1, Alt-Tab safety. Require hardware screenshot/log before labeling final.
