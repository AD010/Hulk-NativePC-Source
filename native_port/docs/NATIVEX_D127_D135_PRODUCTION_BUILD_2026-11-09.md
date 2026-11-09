# Production NativeXRenderer upgrade D127–D135 — source gate successful

As of the NativeX #35 GitHub Actions job, **all 43** ordered Hulk source patch/test steps ran successfully on the REAL `919371b5bdae0c9c4c7120ca321f5a4d72b7e78e` checkout using `ci/test_native_x_patch_chain.py --candidate`. That establishes source transform compatibility, **not a successful compile or game rendering**.

This commit makes `native_port/BUILD_WINDOWS.ps1` checkout the same new SHA, sets `HULK_NATIVEX_SOURCE_SHA` to the actual checkout SHA for each independently fail-closed Python patch, and turns the new-SHA source preflight into a **blocking** Windows Actions step. The old pinned-SHA source check remains available as a regression test. No second workflow, branch, or redistributable proprietary assets.

For stability triage the new optional Vulkan features start conservative:
- `native_x_renderer_dynamic_rendering = false` retains the prior render-pass approach, avoiding an unvalidated D130 change on this NVIDIA driver.
- `native_x_renderer_pipeline_library = "off"` disables D132 graphics pipeline libraries initially; earlier crash was inside `nvgpucomp64.dll`, so do not add driver compiler variants until engine stability is tested.
- Persistent `native_x_renderer_pipeline_storage = false` remains to avoid historical NVIDIA cached-pipeline exception.
- Shader scale constants (D128), command queue wait fix (D135), upstream target/resolve improvements and the upstream public API 0.7 stay compiled in as available. Hulk's special MSAA sample-grid alias and GPU PM4/surface patches remain enabled.
- `Profile::unscaled_screen_fractions` defaults **false** for this title until its conflict with 640x360/4x and 1280x720/1x shared surface ownership is tested; D127 implementation is present but not blindly applied.

Gate for promotion: this new upstream must now **compile** under the existing Windows runner, pass source-free ZIP verification, and display a correct main menu, 3D geometry, depth, resolve, alpha, flicker-free city; then survive repeated sessions / Alt+Tab without crash. Versioned ZIP goes only to the PRIVATE release and previous successful ZIPs are preserved. If compile fails inspect PRIVATE compiler diagnostics; don't switch XEX region, overwrite private assets, or declare a finished port.

Crash collection patch in main is source only: see `native_port/docs/HULK_WINDOWS_CRASH_REPORT_MINIDUMP_2026-11-09.md`. It produces a small local-only minidump on an unhandled exception, not an automatic crash fix.
