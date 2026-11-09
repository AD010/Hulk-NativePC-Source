# NativeXRenderer D127–D135 migration gate — November 9, 2026

This is **not yet an upgrade of the shipped Hulk renderer**. The current, user-tested production Hulk NativeXRenderer revision is `bf965e952c98e050753e91d08f32685a9f754b12`; upstream now has `919371b5bdae0c9c4c7120ca321f5a4d72b7e78e` (17 commits ahead). The user requests combining upstream progress with every existing Hulk patch rather than indefinitely staying pinned.

## Evidence from the upstream diff

- D127: explicit small screen-fraction 1× targets, box-filter scale-down. This touches `src/renderer.cpp`, `src/targets.*`, `src/blit.*`. **HIGH CONFLICT** with Hulk's custom aliasing of 640×360 4× and 1280×720 1× views, custom resolve path and physical target storage.
- D128: shader-system constants hold render scale, including pixel position of offscreen targets. Changes `src/draw.cpp`, `src/shaders.*`, `src/spirv_patch.*` and pipeline translation. Potentially useful for visual correctness but requires old patches' order and shader compilation verification.
- D129–D133: pipeline lists, resource signatures, graphics pipeline libraries, pipeline storage format and fast startup. These are performance features, **not proof** of fixing persistent black/green polygons. Hulk explicitly disables persistent pipeline storage after NVIDIA driver crash and should not turn it on without testing.
- D134: viewport max dynamically constrained by GPU's Vulkan limit. The user's NVIDIA max 32768 generally did not hit the AMD 16384 issue, so not a likely primary flicker fix.
- D135: GPU command queue scheduling/waits. Could reduce CPU scheduling overhead; cannot fix invalid PM4 attachments or unsupported primitive semantics by itself.
- Upstream bumped NativeXRenderer public API version from 0.6 to 0.7: build/link integration must be retested after source patches pass.

## Actual migration mechanism

The 22 source-only `ci/patch_native_x_hulk_*.py` scripts now honor an explicit `HULK_NATIVEX_SOURCE_SHA` when launched by the integration verifier; by default they still enforce the original pinned SHA. The verifier can run `--candidate`, checking out the REAL candidate commit, then applying **all 43 production ordered tests + patches** to it, stopping at the first concrete failing file/anchor. It never substitutes an old SHA with newer files, publishes no game bytes, and leaves the shipping Windows build pinned until all source and compiled integration gates pass.

The existing Windows GitHub Actions workflow probes the candidate as a separately labeled **non-blocking source-only step**, then builds Hulk using the original known revision. A successful source-only result is necessary but insufficient: new upstream CMake changes, DiligentCore patches, public API version, shader signature and real game rendering must still be tested.

## Switch criteria

1. `python ci/test_native_x_patch_chain.py --candidate` completes all 43 steps; fix each failed local anchor with a tested old/new revision-aware transform.
2. Adapt the **actual** `native_port/BUILD_WINDOWS.ps1` checkout SHA, NativeX CMake/DiligentCore dependencies and all interface version requirements and pass clean Windows compile and ZIP verifier.
3. Run independent Hulk 3D menu, city and HUD checks on the user's RTX 5070Ti, preserving 16:10 2560×1600 DPI175%, looking for flicker, stale target reuse, polygons, black holes, MSAA alias conflicts, driver crashes, shadow/light correctness and full scene composition. Keep known-good ZIP for rollback.
4. Only then promote candidate as production; do not claim that upstream updates alone cure the recorded scene artifacts.

No new workflows, branches or game payloads are needed.
