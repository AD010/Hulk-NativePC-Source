# Hulk: same-sample-grid image ownership (NativeX Vulkan)

Evidence: user log incredible_hulk_131.log creates 1280x720/1x color
base1024 pitch1280 and 640x360/4x color base1024 pitch640 at
2560x1440 host size. Equivalent depth base0 views are also created.
NativeX \`RenderTargets::SurfaceKey\` keys by guest MSAA, pitch and size,
so the two views otherwise allocate independent Vulkan images.
The previous reverse-Resolve alias addresses an individual Resolve but
not Draw/Clear writes between views.

This change keeps two HostTarget records with separate guest view sizes
and per-view scaling, but reuses one Diligent native texture/RTV/DSV/SRV
for exact matching EDRAM base, sample-grid pitch/size, guest storage
format, color vs depth and full host dimensions. Synchronizes the last
write generation on each read/lookup. Does not alias shadow/reduced views
or unrelated surfaces; keeps the original XDK 6534 hooks and raw input.
Pinned upstream: bf965e952c98e050753e91d08f32685a9f754b12.

Verification: compiled synthetic C++ fixture tests both lookup directions,
color and depth, mismatched base/pitch/format/dimension, shadow/reduced
exclusion and resource dimension mismatch. Full Windows CI build and
RTX5070Ti visual proof remain separate gates. Native Vulkan may still
have shader/depth/indirect PM4 issues; this is **not** a final release.

No Xbox XEX, assets, guest PPC source or RenderDoc captures included.
