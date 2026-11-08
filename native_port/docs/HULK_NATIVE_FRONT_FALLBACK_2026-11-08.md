# Hulk NativeX black-screen recovery — source test

User's NativeX log 079 proves 31800 completed frames and many shader
draw/resolve commands yet black output. NativeX PresentFrontBuffer uses
ONLY a fetch from g_front_buffer+objects.texture_fetch, then
g_resolved.FindNewest(fetch_address). An invalid/missing fetch clears
the screen black despite other successfully rendered color targets.

New pinned-source patch selects the most recent valid nonshadow
noncube color Resolve when a direct front lookup fails. A matching
1280x720 guest color Resolve is preferred; otherwise the most recent
large color Resolve (<600 native-present frames old) is considered.
Use its own guest format and identity swizzle. Never replace a valid
front target. Read-only [HULK/NATIVEX] logs include
fallback, selected address and guest/host sizes.

This is a *hypothesis-driven workaround* pending runtime validation;
it does not prove guest draw pixels correct and does not transfer TU1.
Users can opt out with native_x_renderer_hulk_front_fallback=false.
NativeX defaults OFF until gameplay verifies a correct image.
No guest PowerPC or raw input changes. Render scaling remains available
through native_x_renderer_resolution_scale.
