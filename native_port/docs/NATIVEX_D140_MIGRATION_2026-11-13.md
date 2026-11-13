# Hulk NativePC — NativeXRenderer D140 migration audit
Date: 2026-11-13 (UTC). Branch: `ci/native-x-d140-migration`. **Not a release**.

## Verified revisions
- Existing release: Hulk NativePC source built against NativeXRenderer `919371b5bdae0c9c4c7120ca321f5a4d72b7e78e` (D135).
- Latest upstream checked: `fa19ea85fec5c7f7f8a2df82d6c4a86548817b52` (D140, 2026-11-11), six commits ahead.
- Working `main` and its sole manually triggered Windows Release workflow are deliberately unchanged.

## Upstream features being integrated
1. D136: pipelines lists from compressed game shader archives and changed vertex shader exports. Requires an **actual Hulk .arc/index shader reader** before it accelerates Hulk; merely upgrading the library does not populate a list.
2. D137: greater precision in alpha resolve for `2_10_10_10_FLOAT` targets; verify against Hulk's physical surface/Resolve aliases.
3. D138: sample-rate alpha-to-coverage dither for 1x targets, which changes blending semantics; requires visual regression coverage.
4. D139: texture, render target and pipeline memoization, fewer clock reads, and load-address shader memo. This supersedes the Hulk's selected D139 fast path; never install duplicate memo.
5. D140: lean setters, fenceless draw queue and shadow comparisons. High risk for Hulk's PM4 gap state, nested commands, replay/fences, Present and inline copied vertex/index bytes.

## Migration isolation / automated checks
- The candidate runner `python ci/test_native_x_patch_chain.py --d140` fetches and checks the exact D140 commit, runs the complete production-ordered patch list against it, and fails closed on the first mismatched C++ source anchor.
- Hulk source unit tests that import a known D135-only module run with the old revision **in their own subprocess**; the actual candidate transform is separately challenged with D140 C++.
- A candidate-specific adaptation retains upstream D139 lazy clock reads with Hulk's frame-shared 200 ms shader/pipeline wait.
- A candidate-specific D139 adapter recognizes and reuses the upstream-owned shader memo, requiring full byte comparison and reset instead of injecting a second 256-entry cache.
- No new repositories, no new persistent workflows, no private game assets or Release action are used for this audit.

## CI history and current blocker
- Source-only CI #96 (2026-11-13) verified public source and passed the first eight NativeX source transformations through `patch_native_x_hulk_frame_path_probe.py`.
- It stopped before MSAA patching because `test_patch_native_x_hulk_msaa_grid.py` imported a legacy pin-limited module under the D140 SHA. This is test-loader incompatibility, not proof of a MSAA rendering failure.
- Candidate test-runner updated to load old-regression tests under their original pin while checking actual transformations on D140.
- **No full candidate NativeX compile or Hulk gameplay validation yet.**

## Second automated audit

After the first audit exposed a legacy module import-time pin, the
D140 probe now isolates old unit-test imports and subjects each patch's
actual C++ transformation to the latest D140 source separately. Only
a source-only GitHub runner is used; no Xbox game input is fetched.

## Mandatory acceptance gates before upgrading main
- Complete sequence of production-ordered Hulk patches applies on latest upstream with every regression test passing.
- NativeX C++ compiles on Windows x64 Release without turning off the source checks.
- Preserves real Present/fence/Alt+Tab, command boundary, guest memory and shader/texture ownership and native 2560x1600 presentation.
- Recheck GPU verifier VS/PS program state, fetch constants, Resolve aliasing and HUD/pause rendering against matching real frames, not aggregate counters alone.
- High-FPS selection must not be represented as physics decoupling, and the EU Title Update water fix must not be represented as compatible with USA XEX.
