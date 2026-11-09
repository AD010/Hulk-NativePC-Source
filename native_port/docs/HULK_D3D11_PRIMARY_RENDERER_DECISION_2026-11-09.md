# Hulk NativePC graphics architecture decision — 2026-11-09

## Decision

Target a **native Direct3D 11 renderer at D3D feature level 11_0** as the preferred future graphics backend for The Incredible Hulk (2008) Xbox 360 recompilation. Preserve the existing Vulkan/NativeXRenderer code and all user-provided XEX/assets while the replacement is implemented and validated. The long-term deliverable is an ordinary x64 Windows game, not an exposed ReXGlue/Xenia user interface.

This is a direction/architecture decision, **not an implemented D3D11 backend** or proof of a working scene.

## Scope and boundaries

- Target D3D11 hardware with **feature level 11_0**; no engineering requirement to support the GeForce 9800 GT or DX10 FL10_0.
- Do not impose AVX/AVX2 unnecessarily on the x64 guest runtime. Audit generated PPC code, ReXGlue, codecs and dependencies separately; SSE2 as a *proposed* baseline, subject to test.
- Do not switch to Direct3D 12 or write a Direct3D 9 backend as part of this migration.
- D3D11 is a Windows graphics API, not a guarantee of frame fidelity, universal GPU compatibility, low system requirements, or improved performance. Vulkan is also native: the goal is a more complete high-level rendering path.
- One existing public source-only `main`; one existing workflow. Do not create extra CI workflows, branches, private game payloads in public Git or new automation.
- The private `hulk-cloud` game input and source-only releases remain separated.
- The current deployment has one `incredible_hulk.toml` with CVar dependencies. A future **single user-facing Hulk.ini** may replace it only with an explicit boot-time migration/validation and read-write bridge; never rename TOML and assume the runtime understands INI.
- D3D11 code should be written for Hulk (Edge of Reality), not copy GPL-3.0-licensed implementation of The Darkness into this MIT repository without appropriate relicensing/compliance.

## Relevant engineering models

**The Darkness Recomp (Xbox 360)** uses game engine geometry, authored shaders and finalized engine state with a D3D11 backend rather than reinterpreting GPU command packets; its current D3D11 device creation requests `D3D_FEATURE_LEVEL_11_0`. Its MSAA modes allocate genuine multisample color/depth surfaces and explicitly resolve; these are architectural examples, not drop-in code.

**Splinter Cell: Double Agent XBE Recomp (original Xbox)** translates its game's D3D8 calls into a backend interface, currently implemented as D3D12. The separation of render calls from backend implementation is a reusable *design*, but its D3D8/XBE engine details and code are not Hulk/Xbox 360 compatible.

Source references: `portingpete/The-Darkness-Recomp/RENDERING.md`; `renderer/d3d11/world_renderer.cpp`; `renderer/d3d11/display_context_d3d11.cpp`; `TGP482/SCDA-XBERecomp/src/gpu/backend.h`; `src/xdk/d3d8.cpp`.

## Proposed phased implementation

1. **Establish known input and evidence:** preserve current USA XEX pin, XDK 2.0.6534 profile, asset validation, TU1 audit, current Vulkan captures and known GREEN/quarter-frame references. Repair current CI regressions first; build success is not visual validation.
2. **Native scene extraction:** identify genuine Edge of Reality draw/material/geometry/texture and submission structures and verify them with read-only hooks. Capture vertex/index buffers, transforms, texture bindings, render passes and final composite *before* lower-level PM4 packaging where possible; do not invent missing engine structs.
3. **Host-API-independent render commands:** define versioned typed data for RenderTarget/Depth, Clear, Draw, Copy/Resolve, Present, texture upload, pipeline state and synchronization, with surface identities stable across guest 640x360/4x MSAA and 1280x720/1x views.
4. **Minimal Direct3D11 backend:** create D3D11 FL11_0 device/swapchain and a verified scene target, vertex/index upload, shader pipeline, color/depth ownership, visibility, 4x native MSAA with correct Resolve, and final composed 16:10 screen placement (2560x1600 -> 2560x1440 centered with correct HUD). Start with real controlled passes instead of a giant renderer rewrite.
5. **Validate frame by frame:** compare target size and ownership, draw count and geometry, texture contents, depth/stencil and pixel history with native Vulkan evidence and console/Xenia references. A green 3/4 image, offscreen scene, quarter-sized image, incorrect alpha, missing geometry or missing shader must fail acceptance.
6. **Introduce optional D3D11 selection** only after D3D11 reaches reproducible playable 3D scenes. Vulkan remains a comparison/fallback during development; never switch the distributed default prematurely.
7. **Deliver official-style UX:** original game's own Settings/Video/Controls/Audio/Language/Exit paths, true Win32 mouse/keyboard and icon prompts; one user-facing `Hulk.ini` saved safely, valid value ranges and restart/apply semantics. Diagnostic console is not the end-user menu.

## Acceptance gates

- Windows x64, FL11_0-compliant hardware, no mandatory AVX/AVX2 after validated toolchain and dependencies.
- Full native 3D game scene with correct geometry/materials/lighting/water, RT/depth aliasing and scene composite, no green/black frames or quarter-frame.
- Independent graphical resolution and anti-alias controls (Off/2x/4x where supported), functional Resolve, no destructive guest-register tricks.
- Correct mouse camera, controller-free KB/M, 16:10/high-DPI and robust alt-tab/restart.
- Real native game menus and persistence. Title Update behavior and Russian text handled as separately verified tasks.

Do **not** assume a backend switch by itself fixes missing PM4 draw bindings or pixel coverage; the prerequisite is reliable source data and render-pass reconstruction.
