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

## Third automated audit: real shared image ownership collision

D140 source-only run #97 passed thirteen early post-patch stages (through
MSAA, reverse alias, depth/stencil). The first genuine source collision is
`ci/patch_native_x_hulk_shared_storage.py`'s old RenderTargets::Get
lookup anchor. Upstream D139 introduced a four-entry recent-target cache.
The candidate adaptation synchronizes `HostTarget::written` on both the
recent-cache and the map-hit paths and remembers a newly shared guest view.
Its D140 string anchor was verified unique against upstream `targets.cpp`;
the new regression checks both alias paths and idempotence.

## Fourth automated audit

Run #98 reached the new D140 shared target regression but its source-only
unit assertion incorrectly prohibited returning a cached target at all.
It now requires the shared-write epoch synchronization to happen BEFORE
returning from the D139 cache, retaining the optimization without stale
Hulk EDRAM alias state. The actual upstream C++ anchor was verified unique.

## Fifth automated audit: minimized pending-frame compatibility

Source-only #99 reached `patch_native_x_hulk_minimized_pending_swap.py`
after passing all earlier PM4 register, shader prescan and Resolve fixes.
NativeX D140 adds the `check_time` argument to PresentPending and calls it
from command processing more often. The candidate now gates only the
three branches that actually call ShowPendingFrame: before the pacing
statistics, not before each queue poll. The original D135 path remains
unchanged and both paths have regression tests.

## Sixth automated audit: source preflight dual-version regression

#100 passed D140's pending swap code path and advanced to the Hulk
frame-shared scene wait test. Its legacy assertion expected a single
D135-style Python import, while the candidate preflight now imports both
D135 and D140 exact wait snippets. Updated the test to verify both
variants, without removing either C++ source marker or timeout assertion.

## Seventh source-only audit: PS provenance and D139 address-cache reset

#101 applied the source patches through scene wait, driver compilation,
incomplete Draw and PS origin. It stopped in the PS recent-load reset anchor:
D139 inserted the original `loaded_at_ = {};` between `programs_ = {};`
and `record_.Clear();`. The migrated patch now accepts that layout and
resets both the upstream shader-address memo and the Hulk pixel-program
provenance counters, without inserting a second address cache.

## Eighth source-only audit: distinct upstream and Hulk shader caches

#102 passed the D140 Present, scene wait and PS recent-load patches.
It exposed a false-positive in the *new* D139 idempotence check:
the substring `loaded_at_ = {};` also appears inside Hulk's
`hulk_loaded_at_ = {};`. The D140 detection now checks the exact
standalone C++ statement, and the regression checks both real variants.
No shader bytes or GPU commands are changed by this correction.

## Ninth source-only audit: D139 PM4 scanner structure

#103 completed the D140-aware XDK pixel-program provenance and confirmed
upstream D139 memoization is not duplicated. It stopped at the read-only
first-failure scanner's old sentinel `bool FetchSlotDirty(`, which upstream
D139 replaced with `BitReverse/DirtySlots`. The candidate instrumentation
now recognizes either exact reviewed top-level function sentinel and
otherwise fails closed; a separate source-only regression covers D140.

## Windows candidate integration — source compilation gate

- NativeX checkout now uses exact D140 `fa19ea85fec5c7f7f8a2df82d6c4a86548817b52` in the **candidate branch** `BUILD_WINDOWS.ps1`, not a spoofed D135 HEAD.
- All 45 Hulk patch scripts and 41 patch unit tests use `ci/native_x_d140_dispatch.py`; every patch still checks its real D140 source anchors and revision, and old test fixtures run in their originally supported environment.
- The public D140 audit uses the **same** patch dispatcher as Windows, with an additional source-only parity test verifying all 86 builder steps and strict revision validation.
- Stable `main`, sole manual Release workflow, and prior ZIP #95 are not replaced. Windows compile, render verification and frame-rate-sensitive physics still require independent gates.

## Mandatory acceptance gates before upgrading main
- Complete sequence of production-ordered Hulk patches applies on latest upstream with every regression test passing.
- NativeX C++ compiles on Windows x64 Release without turning off the source checks.
- Preserves real Present/fence/Alt+Tab, command boundary, guest memory and shader/texture ownership and native 2560x1600 presentation.
- Recheck GPU verifier VS/PS program state, fetch constants, Resolve aliasing and HUD/pause rendering against matching real frames, not aggregate counters alone.
- High-FPS selection must not be represented as physics decoupling, and the EU Title Update water fix must not be represented as compatible with USA XEX.
