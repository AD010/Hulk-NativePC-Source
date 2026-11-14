# Hulk NativePC D140 — screenshot and Vulkan log 201 forensic report
Date: 2026-11-14, observations at 11:11–11:15 local Ukraine.

## Status (do not conflate)
- Isolated candidate Actions #106 compiled Windows x64 successfully from
  `ci/native-x-d140-migration` at `48818af688d5`. Latest confirmed
  upstream NativeXRenderer is `fa19ea85fec5c7f7f8a2df82d6c4a86548817b52`
  (D140; 2026-11-11). Working `main` remains on the D135 pin.
- User's #201 log shows NativeX Vulkan running at 2.0 internal scale
  (1280x720 guest -> 2560x1440 host) on a 2560x1600 swapchain.
- Screenshots show low-frequency/blurry world billboard lettering at ×2,
  plus user-observed red/yellow building flashes. A screenshot does not
  establish whether the sign's base texture has higher-resolution mips.

## Measured sustained workload (log 201, frame 25800)
- GPU frame time ~14.88 ms average over 600 frames (~67 FPS rendering
  ceiling, NOT guaranteed end-to-end simulation speed); max 30.01 ms.
- 1696 draw calls, 1696 drawn; 3316 buffer copies, ~10625 KiB uploaded.
- Renderer CPU ~7.68 ms, wait for GPU ~6.54 ms; 600 SMAA passes/600 frames.
- 1322 MB device memory used; guest textures ~383 MB; 10 render targets,
  20 resolved textures; 0 shaders/pipelines compiled in this warmed sample.
- No sustained VRAM leak is established from this stable snapshot.
- Do NOT attribute the user's 96% GPU utilization to D140 regressions
  without same-scene, same-scale GPU measurements on the preceding build.

## Render correctness deficits
- Two shader analysis failures (27-word vertex IM_LOAD_IMMEDIATE) with
  access violation; one SDK shader translation error involving computed
  texture LOD and pixel shader implicit derivatives.
- Existing `tools/audit_hulk_shader_pm4_overlap.py` parses *one* of
  these failed 27-word shader dumps as an adjacent plausible PM4
  LOAD_ALU_CONSTANT -> IM_LOAD chain. This is evidence to inspect XDK ring
  ownership, not sufficient proof that the entire buffer is nonshader data.
- >=140000 HULK/INCOMPLETE-XDK-DRAW rejections by end of session. Of 30
  emitted warning samples, 22 say the last fully parsed header was
  `0xC0025800` (EVENT_WRITE_SHD); 17 failures are exactly at their
  last_good_end. The rest may contain PM4/data interleave.
  **Never use EVENT_WRITE_SHD alone as an assumed stream terminator.**
- Invalid fetch descriptor samples remain: slot 0 type2/dim0 word0
  `0x8a004802`, slot 1 type2/dim0 word0 `0x820000fe`.
  Seven `HULK/FETCH-WRITER` samples are classed as SET writes; among
  their words are IEEE float values for 1280 and 720 in fetch slots.
  This could be register-target corruption, owner shadow drift, or
  authentic guest data; prove source packet owner before dropping writes.
- NativeX reports *zero* failed Draws in the sustained render summary:
  that count does **not** include the CPU-side rejected XDK scans
  above and does not prove the color flashes are fixed.
- Automatic GPU verifier at frame 900 checked 0 matched D3D draws,
  so this log **does not prove** VS/PS parity in city scene.

## Changes actually committed in the D140 candidate after log201
1. Removed collision with SDK/runtime `gpu_guest_fps_limit` by migrating
   the Hulk-specific optional submission cap to `hulk_guest_swap_fps_cap`.
   In addition the cvar is declared **only once**, in
   `native_port/src/native_x_renderer_hulk.cpp`; the SDK kernel
   `VdSwap_entry` reads it with `rex::cvar::Query<int32_t>`. Defaults
   to zero: original guest time/physics unchanged. This is not 60 Hz
   physics decoupling or proof that every title can submit at 60+ FPS.
2. Defaulted `native_x_renderer_trace` and `native_x_renderer_verify`
   to false for ordinary retail runs, retaining them as opt-in diagnostic
   cvars. Source-only unit gates updated. This avoids always-on CPU-side
   capture costs, not the physical 2560x1440 GPU workload.
3. Stable 2.0 host scene scale and existing MSAA/Resolve/HUD alias ownership
   are intentionally preserved; no indiscriminate LOD bias or altered
   texture descriptors applied to hide visual corruption.

## Next hard acceptance gates before main promotion
- Reconstruct real PM4 command ranges by identifying shader/vertex/index
  payload ownership from XDK PPC call metadata, not by guessing a tail
  delimiter. Guard against shader microcode containing prior ring PM4 data.
- Verify VS float constants and used texture fetch descriptor dwords across
  GPU packet and CPU XDK snapshot at *the exact flashing city Draw*;
  only then patch SetConstant / descriptor ownership semantics.
- Prove whether each blurred sign uses its original base mip,
  an accidentally clamped mip, or a genuinely low-resolution Xbox asset.
  Native 2× geometry resolution cannot synthesize absent image texels.
- Profile CPU upload and GPU draw costs without trace/observer, preserving
  textures, postprocessed frame, 4× MSAA and HUD.
- Independently validate 60+ FPS with correct game physics, and map the
  authenticated EU Title Update water state machine to USA XEX PPC code.
  The TU1 XEXP itself must not be applied across regions.
- Do not trigger more Windows/Actions builds without the user's command;
  do not ask for repetitive manual tests or claim an official-quality port
  from a compiler-success status.

## Main D140 integration build (user-authorized, 2026-11-14)

Merged migration branch via PR #1, squash main `c811a20d72bd5d1c526df1fc40bdb6cb9991f783`.
The existing single Windows x64 Release workflow now gates NativeX D140 source
adaptation and FPS/UI tests before private original XEX download. This audit
entry is the one-shot user-authorized trigger for a new versioned ZIP;
previous #95/#106 private assets are preserved. A successful CI does NOT
prove the observed color flicker, sign textures, 60+ FPS physics or TU1
water behavior correct. It proves only the named compilation, package,
and staged source tests.

## Windows release retry after preflight guard (2026-11-14)

Actions #107 stopped in the public test `test_release_workflow_remains_manual_and_main_only` because its one-shot document-triggered workflow was not accounted for by the test. That failure happened before proprietary input download and before compiling D140. The updated gate explicitly permits *only* this temporary, exact documentation-only trigger on `main`, disallows PR/schedule or broader pushes, and restores the sole workflow to manual-only after the new Actions run begins. This is a user-authorized rebuild and does not waive any NativeX C++ source test or gameplay-fidelity checks.

## #108 missing text / log 202 regression (2026-11-14)

Actions run 43585479558 succeeded at f81c55f5bb0ea8e6e5e3dc63ffd0da83facb64d0.
The user reports missing startup/menu text and persistent colored flicker.
Log `incredible_hulk_202.log` shows warmed menu frames with 10 submitted and
10 drawn calls, no translation/pipeline waits. Those counts cannot prove
that missing text draws were submitted at all. Gameplay still reports invalid
vertex slot 95; neither that symptom nor flicker is declared fixed.

D140 enables `g_lean` when the renderer runs without trace/observer and
`native_x_renderer_lean_setters` is true (the upstream default). Disabling
trace/verify for #108 thus enabled a previously inactive capture path:

- `EnterLean` bypasses the patched `StateTracker::Enter` gap scan.
- A setter that emits packets calls `PromoteLean` then normal `Leave`.
- Hulk's `Leave` patch advances `last_hulk_segment_end_` beyond the setter,
  without having replayed the preceding gap. Later normal calls cannot
  recover that skipped interval. An empty setter alone does not advance it.

The application now disables lean setters before NativeX initialization,
including when an existing user's TOML enables them. The shipped config also
sets this false. Full tracked setter boundaries are required until the lean
path implements equivalent Hulk gap replay. Trace/verify remain off; no draw,
shader, texture, depth or lighting effect is hidden to mask the symptoms.

Validation: the D140 full source patch gate passed all 86 steps and 13 public
host preflight programs. The defaults/startup-order regression gate passed
both tests. No Windows Actions build was requested or launched for this fix.
The capture incompatibility is established from source; attribution of all
missing text or color flicker to it remains unverified without game execution.
Visual release acceptance, mip/LOD quality, TU1 water behavior and the other
open port requirements remain outstanding.

## Follow-up: raw log 202 classification and fail-closed D140 startup

The archived `incredible_hulk_202.log` was read directly rather than only
using the screenshot or build status. In its 3,804 lines (2026-11-14):

- The first 16 printed `HULK/INCOMPLETE-XDK-DRAW` warnings include
  ordinary-looking XDK draw boundaries followed by unparseable words, such
  as `0x3f800000` and `0x44800000`. Several follow a valid
  `0xc0025800` EVENT_WRITE_SHD. The warnings are throttled; 16 is **not**
  the number of total rejected draws. Do not treat EVENT_WRITE_SHD as an
  unconditional PM4 end marker or accept partial state on speculation.
- All five startup `NtCreateFile FAILED` paths are the same in archived
  logs 200, 201 and 202 (UKENGLISH index and four publisher movie files).
  They precede #108, so the file-not-found messages alone cannot explain
  the newly missing menu text.
- Three `HULK/INVALID-VERTEX-DRAW` warnings reference slot 95:
  type 0, base `0x9b8000`, 69,376 words (twice), and type 2,
  base `0x840`, zero words (once). All were correctly rejected by the
  existing fetch bounds guard. A shader-used mask or register ownership
  mismatch remains possible; discarding this guard could expose invalid
  geometry rather than repair it.
- The logged scene Resolve samples show a 640x360 4xMSAA guest
  intermediate represented at 2560x1440 on both source and destination.
  Therefore those samples do **not** demonstrate an intermediate
  downscale. The blurred signage still requires texture/mip provenance.
- Source confirms the #108 D140 lean setter path can skip PM4 gaps.
  The first correction disabled it before NativeX Initialize. As a
  further safety refinement, Hulk now unconditionally forces that cvar to
  false and refuses NativeX startup if the value remains true. The
  startup invariant is asserted in the existing USA GPU defaults tests.

These changes are source-level safeguards and diagnostic classification.
They have **not** yet received a later Windows Actions release, game-frame
comparison or verified restoration of the missing menu text. The existing
single `workflow_dispatch` Release workflow remains manual-only.

## Fix: consume PM4 gap exactly once across empty XDK setters

After the D140 lean-setter disable, source review found a separate missing
cursor advance in the existing upstream `StateTracker::Leave` early-return:

`skip_empty_calls_ && !draw && end == p.begin && p.scan_from == p.begin && initialized_`

Hulk's patched `Enter` has already examined an unhooked PM4 gap before
the setter's `Leave`. Before this fix the early return left
`last_hulk_segment_end_` at its former value, so another empty setter
could scan and capture the **same** valid GPU commands again. This can
duplicate draws and wasted renderer work; whether it caused any particular
user-visible colored flash remains unverified.

The existing `ci/patch_native_x_hulk_pm4_gap.py` now consumes the attempted
same-device, monotonic gap up to `p.begin` on the empty-Leave branch.
A range larger than 0x40000 invalidates the cursor (fail closed), without
inventing GPU packets or changing the XDK draw payload. The existing
PM4 patch-chain test checks the integrated marker, and the existing
PM4 stream unit tests cover repeated empty setters, backward/device
changes and oversized gaps. A standalone compilation and execution of
the exact C++ branch passed locally.

This is a **source correction, not a released or visually tested Vulkan
fix**. No Windows runner, new workflow, branch, public game artifact,
or unauthorized private input download was started. The major incomplete
XDK Draw (mixed PM4/data) problem still requires guest source/owner
provenance before any partial packet acceptance. Until that is established,
the existing fail-closed check remains enabled.

## 2026-11-14 PM4 prefix qualification, mip provenance and quality gate

Source-level candidate for the user's explicit next Release:
- The non-Present incomplete-XDK handler now has a *strict* replay qualifier
  for a fully parsed real **single** DRAW_INDX / DRAW_INDX_2 immediately
  preceding the first bad byte. It re-parses only the bounded original prefix,
  and demands identical register-write sequence, predicated writes, LOAD data,
  shader microcode, shader-load metadata, register order, and BIN predicate.
  Only then can it emit the already-complete original draw. Unknown trailing
  data is never submitted, and the command cursor is invalidated.
- All other incomplete draws keep the original fail-closed path. This is not
  a general XDK 6534 boundary reconstruction and not a claimed solution to
  most missing city geometry.
- Counts from the original archived logs: #200 had 17/18 sampled incomplete
  warnings with zero parsed DRAW packets (max cumulative rejection ID 20000);
  #201 had 28/30 (max 140000); #202 had 15/16 (latest sampled ID 16).
  Throttled warning counts are not percentages of all attempts. Almost all
  observed failures happen before a verified draw, so relaxing incomplete
  scans would be unsafe.
- The existing source patch that guards shader-used texture and vertex
  descriptors now includes **opt-in** `native_x_renderer_hulk_mip_probe`.
  In the candidate TOML it emits at most eight real shader-used texture
  fetch descriptors per 500000 eligible renderer draws, with raw six-dword
  fetch constants, target/native scale, actual LOD bias and AF override.
  This does not alter fetch registers, mip boundaries, texture data or shader
  sampling. It is designed to identify why a particular billboard stays
  blurry despite a correctly scaled Vulkan render.
- The candidate TOML retains `native_x_renderer_resolution_scale = 2.0`
  and SMAA, and uses `anisotropic_override = 4` (8x) only for samplers
  eligible under NativeXRenderer D44's normal mip/linear-filter rules.
  The underlying Xbox source texture may still be low resolution; do not
  describe AF as a texture upgrade. This increases texture bandwidth and
  must be measured on the user's scene if GPU-bound.
- From the logs, late native Vulkan render-thread average intervals:
  #200 16.67 ms, #201 14.88 ms, #202 33.58 ms. Different scenes and time
  windows make these **non-comparable** as an FPS regression benchmark.
  None proves Xbox gameplay logic or physics is decoupled from 30 Hz.
  Keep `hulk_guest_swap_fps_cap = 0` (no extra Hulk cap) and do not
  adjust game ticks, vehicle doors or water physics without verified PPC
  source and repeatable 30/60/120 Hz behavior.

Validation performed so far is source/anchor regression and cross-source
consistency, **not** a full compiled Vulkan run or visual acceptance.
The Windows Release action and private ZIP verification are the next gate.

## USER-AUTHORIZED Windows x64 Release checkpoint (2026-11-14)

The user explicitly requested a new candidate ZIP after the bounded
XDK prefix recovery, 2x/8x-AF mip provenance instrument and PM4 empty
setter fix. The existing single Release-only workflow may run once on
this exact documentation checkpoint on `main`. It must pass the full
D140 source-patch preflight and private ZIP verification; no general
push/schedule/PR builder remains enabled. The trigger must be removed
immediately after the run is recorded. Earlier versioned private ZIP
assets must remain untouched.

Do not call this visually verified: missing menu text, city flashes,
signage sharpness, 60+ FPS and correct physics require actual execution
of the produced binary. No speculative 30 Hz physics detachment or
arbitrary Xenos memory interpretation was introduced.

## User-authorized Release retry after #109 public preflight fixture (2026-11-14)

Actions #109 failed **before** accessing the private XEX at
`ci/test_patch_native_x_hulk_pm4_gap.py`: its synthetic fixture did
not include the new `EMPTY_LEAVE_ANCHOR` required by the actual
source transform. The existing test fixture and assertions have been
updated in `main`, with no relaxation of the real PM4 correctness gate.

Run once more with the existing D140 Windows x64 Release workflow;
restore manual-only dispatch immediately once the run is recorded.
Previous versioned ZIP releases remain untouched. A passing full
source gate is not evidence of visually correct graphics.

## #111 scoped retry (2026-11-14)

#110 passed the previously failing PM4 empty-setter fixture, then stopped
at `ci/test_patch_native_x_hulk_draw_fetch_guard.py`. The new mip probe's
Python transformer had mistakenly encoded a literal backslash-n in the
`#include <bit>` source anchor. The transformer is corrected to use an
actual newline. No GPU correctness condition or invalid-fetch guard was
relaxed. Run the one existing Release workflow again on this user-authorized
documentation checkpoint, then restore manual-only. Previous ZIP files
remain untouched.

## #112 pin-compatibility QA retry (2026-11-14)

#111 reached the genuine upstream source transform and rejected a
D140-only mip probe anchor on the legacy NativeX pin. The mip probe was
moved directly after the common shader-used RequestTextures call, with
its cvar registered beside the common native draw-log cvar. The emitter
still logs only bounded raw fetch and actual scale/LOD provenance and
does not modify texture data, shader state or physics. All three pinned
upstream source shapes are now targeted by the same transform.

User-authorized Release QA checkpoint: run the existing Windows workflow
once, then immediately disable the temporary docs-only trigger.

## Log 203 — real Windows Vulkan performance and rendering regression (2026-11-14)

Owner tested **#112** on NVIDIA RTX 5070 Ti Laptop at 2560x1600 output
(2560x1440 Vulkan render, native scale 2.0) and reported persistent
red/yellow building and menu flashes, approximately 92%+ GPU utilization
and visibly uneven destruction/falling-building animation. GPU utilization
is the owner's external observation; the logger does not print a utilization
percentage.

Complete uploaded `incredible_hulk_203.log`: 7,266 lines,
16:31:10–16:33:53 local, clean application termination. It confirms
`[HULK/PM4] Full setter PM4 gap capture enforced (lean OFF)`.
The new #112 `[HULK/XDK-VALIDATED-PREFIX-DRAW]` appeared 12 times
before its first sampling interval, proving this path is executed, **not**
that incomplete XDK packet reconstruction is finished.

- By 16:33:52 the accumulated gap-stream counter reached **1,476,023**
  original packet-draw replay attempts, and the gap-draw capture counter
  reached **136,000**. Do not equate these aggregate counters with the
  number of completed host raster draws or unique on-screen objects.
- Rejected incomplete XDK Draw counter reached at least **60,000**.
  The sampled failures mostly have `packet_draws=0` before corrupt
  float-like headers; accepting them as packets would be unsafe.
- Multiple real shader/fetch faults remain: 20 sampled
  `HULK/INVALID-VERTEX-DRAW` (including slot 95 and later slot 0),
  8 sampled invalid texture draws, 5 indexed DMA rejections,
  **3 vertex-shader analysis access violations** (27-dword shaders),
  and **2** `getCompTexLOD requires pixel shader implicit derivatives`
  translation errors. No evidence yet that these all share one root cause.
- Gameplay is substantially more demanding than the early menu. At
  frame 15600 (600-frame statistics window): **1,887 actual host draw
  calls/frame**, GPU average **22.36 ms**, CPU render thread **11.54 ms**,
  GPU wait **10.33 ms**, average presented frame interval **23.25 ms**,
  maximum frame interval **122.82 ms**. The command processor spent
  **21.03 ms** per frame at its signals and the game thread waited
  **2.46 ms** average for renderer-queue room. Per frame: **4,524.5**
  buffer copies, **12,484 KB** copied, **12.6** upload-ahead batches
  at around **177.2** draws/batch. This explains stutter without requiring
  an emulated 30-FPS cap.
- At frame 16800, no new shader translation or pipeline creation was
  recorded but the GPU still averaged **17.12 ms** (CPU 10.72 ms).
  Therefore ongoing slowdown is **not solely shader compilation**.
  The reported 92%+ GPU utilization is plausible for this measured
  GPU-bound interval but is not an in-log counter.
- Native scene 640x360 4x-MSAA resolves reach 2560x1440 host; the renderer
  reports about **28 resolves per gameplay frame**, plus conditional SMAA.
  Unconditional reduced-resolution shortcuts would reintroduce blurred
  output or broken screen-space effects; this requires per-pass profiling.
- **Actual runtime settings differ from the repository TOML**: the log
  reports scaling `"smaa"` invalid and substituted with bilinear;
  `AMD CAS at 1.00`; and mip-probe AF override **5** (vs shipped 4).
  `smaa` is an *antialiasing* setting, not a valid scaling filter.
  The exact precedence mechanism (saved F4 cvars, older TOML or CLI
  overrides) has not been proven. Do not infer the user's ZIP contained
  the wrong base configuration.

Changes AFTER #112 (source candidate, **not** in that published ZIP):
- `native_x_renderer_hulk.cpp` now logs effective startup scaling,
  sharpness, anisotropy, upload-ahead and scene waits, and normalizes
  only an unsupported scaling name to bilinear. It retains valid user
  choices instead of silently overwriting them.
- `native_x_renderer_hulk_mip_probe` defaults to **false** now that
  log203 captured fetch words; this removes unnecessary per-draw
  QA instrumentation from ordinary runs.
- The existing single TOML requests `native_x_renderer_upload_ahead = 512`
  rather than the D140 upstream 256. This batches *original queued data*
  and may reduce CPU/render-pass overhead in scenes with ~12.6 upload
  batches/frame; it is **not** proven to reduce GPU load or eliminate a
  122 ms spike. Do not skip Draw, fake shaders, mutate Xenos indexes, lower
  native resolution or alter physics to force a faster benchmark.

Release acceptance still needs separate evidence for menu text, colored
flashes, malformed vertex/shader identity, 2x texture mip selection,
real 60/120 Hz physics, and building-collapse frame-time spikes. No
visual success should be claimed from a new CI archive alone.

## 203 follow-up: exact VS family, bounded CF repair, fetch ownership and no-loss GPU cost triage

Source of evidence: owner-provided local `incredible_hulk_203.log`
(7,266 lines), reviewed without uploading any game logs or shader bytes
to this public source repository.

### Authentic failed shader load provenance

The three failed, 27-dword vertex shaders all say
`IM_LOAD_IMMEDIATE in DrawIndexedVertices` and have distinct packet/source
addresses:

| VS hash | IM_LOAD payload | PM4 packet |
| --- | --- | --- |
| EC7DD173F4761676 | 0xA28635C8 | 0xA28635BC |
| 0F5C6EDD918BC3F8 | 0xA284B668 | 0xA284B65C |
| 5C56143AEB4D1865 | 0xA27A2488 | 0xA27A247C |

The three 27-word copies differ **only at word index 23** (zero based).
Decoding with the upstream Xenos CF packing in
`rexglue-sdk/include/rex/graphics/format/ucode.h` gives three initial
CF pairs. Their direct EXEC ranges are instructions `3..4` and `5`;
including CF there are 18 directly referenced dwords. The varying word
at index 23 belongs to instruction 7, outside these direct EXEC ranges.
This establishes a common *static* failure family, but **does not**
by itself prove correct shader translation, valid dynamic branches or
the exact access-violation instruction. Do not rewrite or truncate the
original guest programs on this evidence.

The SDK `Shader::AnalyzeUcode` memory-export CF traversal also has a
source-visible one-past-end bug: at the final valid successor index
`2 * cf_pair_index_bound_ - 1`, it formerly queued `index + 1`,
then dereferenced it. The existing
`ci/patch_sdk_hulk_cf_bounds.py` now (1) validates the successor index
before memory access and (2) stops at the final legal CF instruction,
recording the existing memexport-end state instead of inventing a branch.
The exact safety and final-index fragments were compiled and executed in
standalone C++20 tests (pass). It is **not proven** that the three
particular 27-word shaders take this memexport path; reproduction with
the full pinned SDK is still required.

### Vertex-fetch and target state identity

The log contains 20 sampled invalid vertex fetches (not 20 total
invalid GPU draws), especially slot 95 with type 0, plus types/addresses
at slots 0, 9 and 48. Eight sampled texture-fetch rejections were also
reported. These are shader-used invalid descriptors, so silently
accepting or rebasing them is unsafe.

The existing Hulk NativeX fetch guards now record the **actual owning
vertex shader hash** (and PS hash for invalid textures) on rejected draws,
and the existing offline log audit groups these by slot/stage. This is
read-only failure provenance. It does not alter shader programs, format
bits, vertex base/size, guest PM4 chronology or render-target bindings.

`HULK/PM4-NO-WRITABLE-RT` reached a cumulative 750,000 in #203,
but that is not the same as an unresolved *writable* color/depth target.
No sampled `HULK/PM4-ACTUAL-MISS` was printed. Consequently a proven
wrong EDRAM object identity or incorrect replacement render target was
**not** established from this log. Do not force Xenos render-target
values or synthesize a surface to silence warnings.

### GPU Resolve and copy cost with no quality loss

The frame-15600 statistics window (not a single destruction frame)
contains 1,887 submitted/drawn host calls per frame,
28 resolves (0 skipped), 22.36 ms GPU, 11.54 ms render CPU,
10.33 ms waiting for GPU, 4,524.5 buffer copies/frame with 12,484 KB
guest data uploaded, interval 23.25 ms average / 122.82 ms max.
A scene with building collapse may exhibit this backlog, but the log
does not label the beginning/end of each specific collapse event.

Do not skip 4x-MSAA resolves or force lower host dimensions without a
proved equivalent source/destination/format/clear sequence. The game's
postprocessing, shadow maps and front buffer depend on exact copies.
Instead, the existing release-candidate TOML already requests the
original-D98 upload batching window of 512 draws (vs 256 in #112);
this is **unbenchmarked**, not a guaranteed performance improvement.
In the source patch `ci/patch_native_x_hulk_gap_stream.py`, expensive
INFO logging of cumulative PM4 draw replays is now throttled at 10,000
rather than 1,000 draws (~10x fewer INFO lines), without changing draw
count, GPU state, geometry or shader work. This reduces diagnostic
CPU/file overhead, **not** 22.36 ms of actual GPU work.

The existing `ci/hulk_render_log_audit.py` now reports per-window
resolves, GPU/CPU time, uploaded bytes, copying, static VS failure family
and shader-owned rejected fetches. Only aggregate metadata and hashes,
never original shader words, are exported.

### Validation and release gate

- Two standalone C++20 safety fragments compiled and passed:
  successor indices 0..5 and invalid >=6/MAX, plus final CF successor
  transitioning to end without enqueuing one-past-end
- Local log203 direct-CF decoding checked all three authentic shader
  records (27 dwords, static bound 3, execs 3..4 and 5, varying index 23)
- Source-level regression cases were committed for CF protection,
  failed-fetch ownership, static VS-family classification, PM4 INFO
  sampling and Vulkan frame-cost parsing
- Full Python CI suite, recompilation of the complete pinned SDK, real
  Vulkan hardware/RenderDoc image comparison and Windows ZIP are **not
  performed in this follow-up**. These fixes are in `main`, but not in
  released #112. The manual-only single Release workflow is preserved

Outstanding: a specific reproducer for the 27-word shader analyzer,
identification of the missing/incorrect fetch and EDRAM states at the
flashing pixel, GPU-timed per-Resolve costs, and comparable 30/60/120 Hz
destruction/physics footage. No remaster-quality claim.
