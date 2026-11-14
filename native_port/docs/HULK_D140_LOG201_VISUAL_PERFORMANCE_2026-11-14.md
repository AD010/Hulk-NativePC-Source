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
