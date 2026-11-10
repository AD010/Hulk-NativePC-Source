# Xbox 360 recomp references: Edge of Time and Sonic 2006 (10 Nov 2026)

Source-only engineering audit for The Incredible Hulk (2008) NativePC. Research is NOT a renderer fix or working-build claim. No original game files, shaders, PPC output, XEX or XEXP may enter public Git.

## Checked repositories and original code

| Project | Verified feature and code location | Hulk adaptation and restrictions |
| --- | --- | --- |
| [EdgeOfTimeRecomp](https://github.com/goliathret/EdgeOfTimeRecomp), BSD-3-Clause | [surfaces.cpp](https://github.com/goliathret/EdgeOfTimeRecomp/blob/main/src/gpu/surfaces.cpp) chooses distinct host scales for main 3D, 1024 shadow depth, texture-camera and half-size targets; [resolve.cpp](https://github.com/goliathret/EdgeOfTimeRecomp/blob/main/src/gpu/resolve.cpp) captures the guest Resolve arguments and source surface header before putting work on a rendering thread; [video settings](https://github.com/goliathret/EdgeOfTimeRecomp/blob/main/docs/Video-Graphics.md) separate guest MSAA, host TAA, shadow resolution and picture-in-picture scale | P0: identify which of Hulk's actual surfaces represent the 1280×720 output, 640×360 4x-MSAA image, depth, half-size postprocess, and shadow; track exact source identity and transforms. The Edge of Time base is 1120×632 and its target identifiers are NOT Hulk identifiers |
| [Project 2099](https://github.com/GenryTheFox0/Project-2099), GPL-3.0 for public code | The repository publishes the installer and tools, NOT the full runtime. README describes a bounded 6ms D3D12 PSO wait budget, background compilation, early HDR PSO warmup and sampler mip LOD bias; says its Vulkan path is unusable and internal scale >1 remains experimental. Its optional 'DLSS 5' is a ReShade/Feeder/RenoDX package, NOT a verified built-in native DLSS engine | Use bounded waiting/pipeline warmup concept after Hulk's true GPU state is correct, and distinguish sampler texture sharpness from city mesh LOD and streaming. Do not copy unverifiable proprietary runtime or use Project 2099's unstable Vulkan branch |
| [MarathonRecomp](https://github.com/sonicnext-dev/MarathonRecomp), GPL-3.0 | [render_resolution.cpp](https://github.com/sonicnext-dev/MarathonRecomp/blob/main/MarathonRecomp/gpu/render_resolution.cpp) separately scales the main framebuffer, half-width attachments, downsample chain, reflection, radar and shadow. [camera_patches.cpp](https://github.com/sonicnext-dev/MarathonRecomp/blob/main/MarathonRecomp/patches/camera_patches.cpp) reads true relative mouse input, maps yaw/pitch to verified camera state and accounts for cutscenes/auto camera. [pipeline_precompile.cpp](https://github.com/sonicnext-dev/MarathonRecomp/blob/main/MarathonRecomp/gpu/pipeline_precompile.cpp) waits for critical compilation during loading while pumping OS events | Directly applicable design: differentiated target scale, genuine mouse camera rather than always emulated right stick, safe level-load prewarm. Its game-specific PowerPC addresses, data structures, target names and GPL code cannot simply be pasted into Hulk |
| [Soleanna Sonic06 PS3](https://github.com/DrummingBird1/sonic06-recomp), MIT | Early PS3-to-native PS3Recomp project: stage/boss START smoke tests, no completed playthrough, one Windows test setup; offers support-report ZIP without game data, 1x–3x settings and PS3-specific D3D12 experiments | Reuse source-only automated scene-test approach, local error-report sanitation and setup validation, not its PS3 RSX renderer or its unverified performance claims |

## Actual Hulk implementation opportunities (priority order)

**P0: Xenos render-target identity and image correctness.**
Edge's resolve code captures source surface header and guest state at command issue time, not after the next frame overwrites it. Compare with Hulk's packet-time RT0 rebasing and the command-state shadow. Hulk's log 173 still had source texture-fetch errors, rejected/ambiguous PM4 bindings and missing shaders. A wider window or an AI/upscale effect does not repair that. Use the source-only Hulk PM4 oracle and RenderDoc data where available; determine the first incorrect Draw and whether source, surface/depth, and viewport/samples match.

**P0/P1: true 2× internal scaling without pixelation.**
Edge and Marathon do NOT blindly multiply every GPU texture by 2. Main scene, scene depth, shadows, reflection, half-size post effects, HUD and resolve copies need separate validated host-size rules. For Hulk the guest 640×360 4x-MSAA view of a nominal 1280×720 scene is significant. Require actual high-resolution rasterization before resolving; do not accept a 720p blit to 1440p as a fix. Synthetic scale tests are acceptable only after original guest surface geometry is confirmed.

**P1: native mouse and keyboard.**
Marathon updates a VERIFIED guest camera object from relative mouse delta, with pitch limits, proper orbit synchronization, pause and automatic-camera handover. Hulk currently includes an opt-in camera direct path in native_port/src/launcher/native_camera_direct.cpp, but real yaw/pitch guest addresses need verification; otherwise it falls back to stick mapping. Find camera setters/callers in Hulk's USA XEX, never paste Sonic camera addresses.

**P1: shaders and stutter.**
Marathon prepares known shader pipelines at level loading and keeps window events flowing so Windows does not appear frozen. Project 2099 uses bounded PSO waiting, but its runtime is not published. Hulk currently uses NativeX asynchronous shaders and an opt-in wait-all diagnostic, not a confirmed solution. Prewarm with validated game-owned shader hashes/pipeline states only AFTER eliminating incorrect shader analysis, texture fetch, and draw packet errors. Never ship copyrighted guest shaders in a public pipeline cache.

**P1/P2: Manhattan city and nighttime scenes.**
Sampler mip LOD bias (Project 2099) can improve sampled texture detail but CANNOT make HiResDataset zones load when the engine instead selects ZoneLowLod. Fix guest EoR ZoneStreamManager residency, actual cutoffs, and HiResDataset loading, then consider native HLOD. Night lighting/shadows require a separate regression scene; do not hide the bad render with fog removal or TAA blur.

**P2: workflow and reproducibility.**
Soleanna's repeatable smoke tests provide a model for scene-by-scene testing. For Hulk: verify menu, first mission, Manhattan street, rooftop skyline, water jump, night mission, 1x/2x, Alt+Tab, and 175% DPI 16:10. Each run should collect source-safe error categories, screenshot/frame references, and whether the GPU pipeline is compiling. Compare against the exact original XEX revision. Do not expose private input or diagnostic logs in public artifacts.

## Adoption boundaries

- Keep one existing Release-only Windows CI workflow; no new yml and no second TOML.
- Respect licenses: Edge BSD-3-Clause can be adapted with attribution; Marathon/Project 2099 GPLv3 need legal review for code reuse in an MIT project. Researching their behavior and independently implementing the design is different from copying code.
- Platform separation: Marathon/Edge Xbox 360 have relevant Xenos issues, Soleanna PS3 RSX is an entirely different path.
- No direct binary inclusion or copying of extracted game models, textures, shaders or XEX.
- This audit is not a proof that Hulk's black sky, RT aliases or ×2 pixelation are now fixed. Test on the user's actual Vulkan GPU is still required.

## Next actionable tasks

1. Analyze packet-time Source/Target/Depth identity and exact 4x-MSAA sample-grid copies in Hulk.
2. Add measured per-pass scale invariants to the existing NativeX patch chain after real Hulk trace validates which surfaces are scene, depth, shadow and post.
3. Derive guest camera yaw/pitch setters using existing input probes and confirm with moving-camera test.
4. After #1–#3 work, add shader PSO prewarm, world streaming HLOD and mission/night tests without reintroducing GPU emulation.
