# Hulk log160: rendering and flicker investigation, 2026-11-09

User's **real screenshot** shows significantly more of the scene than #22: a complete-width title screen and city, but with black background holes, overlaid car copies, green character/scene pixels, missing geometry and visible flickering. This is **not a completed remaster**. No screenshot or CI test may be described as fully fixed.

Log `incredible_hulk_160.log` on RTX 5070 Ti:
- Correct 2560x1440 internal output, 2560x1600 physical output, centered.
- At frame 3600: 2014 Draw, 1965 submitted, 49 without targets, 28 resolves. The PM4 recovery side separately logged ~540,000 ambiguous per-packet attachment rejections, and hundreds of thousands of queued PM4 draws. These counts are cumulative and do **not** mean 540k geometry objects are missing in one frame.
- Over 600 frames, shader/pipeline preparation skipped 175+160 draws; 30 steady-state offscreen passes were not waited for.
- 48 unsupported primitive-type errors, 114 implausible 1D texture widths (5,891,328), and malformed texture fetch information. These can indicate incomplete GPU register or XDK resource reconstruction, not merely absent Vulkan features.
- Postprocess/scene 640x360 / 4x guest MSAA resolves into smaller guest surfaces; current Hulk-specific shared 1x/4x aliases mean simply importing upstream's new D127 screen-fraction handling could conflict.
- NativeX upstream advanced 17 commits from `bf965e952c98e050753e91d08f32685a9f754b12` to `919371b5bdae0c9c4c7120ca321f5a4d72b7e78e` (D127-D135). D134 caps viewports to GPU limits but on this RTX viewport max is 32768 so it is **not** an expected fix for Hulk's black/green artifacts. Other changes affect ABI, pipelines and scaling; do **not** re-pin blindly across 40+ locally ordered patches.

Changes in this experiment:
1. Explicitly enable native offscreen shader/pipeline wait, and disable D126's steady-state bypass (`native_x_renderer_wait_offscreen_pipelines = true`, `native_x_renderer_skip_steady_passes = false`). Intended to reduce *some* temporal holes; may temporarily stall CPU; does not change already-shipped shader cache policy. User can revert to prior behavior by switching the latter value to true.
2. Log rejected PM4 attachment identities as `[HULK/PM4-MISS]` with slot 0..4, packet-time surface and info values, late XDK-bound identity and cumulative slot count. Throttled for performance. This is read-only, does not invent an attachment or change rendering.

Next gate: user must compare exact same 3D menu + city position with the rebuilt ZIP. From the new log, calculate whether misses are predominantly RT0, depth, or stale MRTs; then fix the corresponding EoR/XDK render pass, rather than broaden the heuristics to accept invalid draws. Use RenderDoc Pixel History on (a) first black gap, (b) overlapping car, and (c) final scene Resolve to identify last genuine writer and input SRV. Full Vulkan replay isn't available in the current Linux runtime.

A native D3D11 renderer remains the architecture target but is **not** yet implemented. This patch alone cannot meet the final goal.
