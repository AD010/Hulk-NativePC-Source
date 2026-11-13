# Hulk #69 — NativeXRenderer graphics regression and abrupt exit (12 November 2026)

**Source of evidence:** user screenshots (alley VFX, huge black polygons in city, light corruption) and private `incredible_hulk_184(2).log`, 4,753 lines (13:17:59–13:19:40 local). This documentation contains **no game code, user files, vertex buffers, original shader bytes or user paths**.

## Confirmed GPU ownership

The source hook fix in #69 worked: `[HULK/VULKAN] NativeXRenderer active; XDK=2.0.6534, guest hooks=10`. `Host rendering off (sync only)` means ReXGlue's Xenos command processor no longer presents the graphics. Diligent/Vulkan on NVIDIA RTX 5070 Ti actually draws at 2× game resolution (guest 1280×720; native host 2560×1440, letterboxed to 2560×1600). ClearF is working: 9 clears and 28 resolves by frame 2400; 934/934 draws accepted in that frame.

**This active NativeX scene is STILL BROKEN.** The user reports large black flickering triangles, overexposed yellow/green effect geometry, and an abrupt exit. The last log line is `[HULK/PM4-NO-WRITABLE-RT] #350000 ...` after `focus=lost`. No exception code, stack trace or device-loss reason was logged. The exit cause therefore remains **unproven**, and may need WER crash evidence; do not assert shader compilation caused it.

## Actual fault signals in the supplied log

| Signal | Count | Significance |
| --- | ---: | --- |
| Invalid 1D texture width, e.g. 5,891,328 or 2,089,216 | 96 | The native texture decoder is asked to interpret invalid fetch descriptors |
| `Texture fetch ... has "invalid" type` | 123 | Fetch descriptor register state is suspect |
| `Texture fetch ... is completely invalid!` | 55 | Additional invalid descriptor variants |
| `HULK/PM4-DMA-REJECT` | 12 | Draw packets rejected due absent/short index size or invalid primitive |
| 16-bit indices, 32-bit endian swap mode 3 | 2 | ReXGlue `PrimitiveProcessor` explicitly DISABLES `k16in32` for 16-bit index formats |
| `HULK/FETCH-WRITER` `source=SET` | 24 sampled diagnostics | PM4 register slots 1/2 receive 0x44A00000=1280.0 and 0x44340000=720.0 |
| `HULK/FETCH-ORIGIN` | 24 sampled diagnostics | Pixel shader DBEE5B430D8DB5F7 later sees those values as texture fetch slots |

NativeX shader and pipeline compilations mostly return `ok=true`. Successful shader compilation does **not** validate the vertex/index data or texture descriptors.

## Source fixes following #69 (NOT INCLUDED in #69 ZIP)

1. **Physical DMA index endian correction:** `ci/patch_native_x_hulk_packet_dma_snapshot.py` snapshots complete aligned 32-bit words for 16-bit Xenos indices with mode 2 (`k8in32`) or 3 (`k16in32`), then swaps the two 16-bit halves in the **owned copy**, leaving original guest RAM intact. ReXGlue's existing PrimitiveProcessor further treats mode 2 as 8-in-16, mode 3 as none: this yields the correct full byte order. Handles odd indices and half-word starting offsets. Direct draw and indexed packet snapshots share the same mechanism. Adds C++ regression in `ci/test_patch_native_x_hulk_packet_dma_snapshot.py`.

2. **Never read uncaptured LIVE DMA indices from a reused guest ring:** `HulkProcessIndexCopies` now refuses requests with no captured overlap or incomplete ranges instead of calling `processor.Process(result)` against live guest memory. This prevents an invalid geometry draw from reading unowned data; some already-invalid draws may be skipped, not repaired.

3. **Preserve chronological PM4 SET/LOAD data:** `ci/patch_native_x_hulk_gap_register_snapshot.py` retains the values and types from the complete first scan and restores them in the second scan per draw; mismatched sources/register identities are rejected. This extends the already-existing bytecode lifetime patch to GPU register state. The command buffer can be reused between first scan and actual renderer submission.

4. **CI checks:** Existing single Windows workflow tests both DMA handling and register replay, with fail-closed source guards. No new repository, workflow, or game assets. Local independent C++20 regression binaries for index swaps and register replay pass; **the final integrated Windows Vulkan binary has not been built or tested** after these source fixes.

## Still unresolved

The pixel shader fetching invalid 1280×720 screen-dimension values from texture slots 1/2 **may** have a separate underlying state-mapping or game register-lifetime cause; the new register snapshot only addresses changes *between* scans, not game-side or decoding mistakes already present in the first scan. Do not set `gpu_allow_invalid_fetch_constants` blindly, replace invalid descriptors with fake ones, discard entire passes, or claim the new code resolves all visual defects until verified in-game. Capture dump/RenderDoc for a failing draw, index bytes, fetch register provenance, and WER exception code if an actual crash recurs.

Keep **#68 Xenos/FSI** as confirmed image-quality fallback. #69 is first confirmed true NativeX runtime, but not a stable release. The user's other work (TU1 jump, Windows saves, 60+FPS physics, native UI/audio and pop-in) continues separately; avoid losing correct graphics while moving toward a standalone port.


## Follow-up: build #73 gameplay trace \`incredible_hulk_185.log\` and NVIDIA WER dump

Source: owner's 12 November 2026 private runtime log (8,965 lines) and
\`incredible_hulk.exe.18208.dmp\`. This section **does not publish** the dump,
game assets, private executable, shader binaries, memory contents or paths.
Numeric counts below were measured from the log itself; causal interpretations
are labelled as hypotheses. The trace predates #75's invalid-fetch guards.

### Verified frame-900 state comparison

- GPU verification reported **1442** executed Xenos DRAW packets, **1397**
  matched to D3D/XDK Draw calls and **45** not matched. Of the 1397 matched
  calls, **1339 (95.8%)** had the same rebuilt GPU state.
- **58 pixel-program mismatches** were reported; those 58 all used an inline
  program in the observer's comparison. Vertex-program mismatches: **0**.
  The recorded mismatching pixel-program hashes were
  \`2e372ea28cc404b7\` (CPU tracker) versus \`bafe2777b0c7f792\` (GPU
  verifier). This is actionable proof of incomplete shader state replay in
  at least this frame, NOT proof of an incorrect shader compiler.
- Texture-fetch slot differences were **none in the verifier's frame-900
  sample**, even though later game frames emit bad texture descriptor
  values. These findings are not contradictory: they sample different frames.
- The observer reported **192** GPU draw packets in buffers outside the
  command-buffer ring. This can contribute to missing calls, but is not the
  same metric as the 45 unmatched packets; do not subtract them.

### Later gameplay / renderer observations

- **590** completely invalid texture-fetch constants; the sampled
  \`HULK/FETCH-WRITER\` events show actual PM4 SET writes of IEEE754
  \`1280.0\` and \`720.0\` into fetch slots 1 and 2, subsequently read
  as texture descriptors by pixel shader \`DBEE5B430D8DB5F7\`. Source
  provenance is confirmed, but whether the high-level XDK path or the
  original guest itself misbinds those registers remains unresolved.
- **21** warnings of a 32-bit endian swap mode used with 16-bit indices;
  existing per-draw DMA snapshot/half-word swap code protects the
  converted index path, but this log does not show parity.
- **12** sampled \`HULK/PM4-DMA-REJECT\` messages and one rejected malformed
  unhooked PM4 gap. These are remaining missing-draw sources.
- The renderer logged **14999** presented/completed frames and reached
  2310 accepted/drawn calls at frame 15000, with a shared 2560x1440 host
  color/depth image for guest 640x360/4x views. Render acceptance and valid
  color/depth ownership do not imply image parity or flicker-free display.
- **520 shader creation BEGIN and 520 END** messages, and **476 pipeline
  creation BEGIN and 476 END** messages, were paired in the textual log.
  There is no unmatched synchronous driver call immediately before the
  final log entry. NVIDIA can compile asynchronously after these APIs
  return, so paired calls do NOT clear the driver or the supplied SPIR-V.

### Crash facts and containment boundaries

The owner's minidump reports \`0xC0000005\` reading \`0x10\` in
\`nvgpucomp64.dll\` (the NVIDIA driver compiler). This identifies
the faulting module, not the first corrupt register or SPIR-V instruction.
Print Screen coincided with the crash but no causal keyboard-path link is
established. Earlier \`HULK/SPIRV-PREFLIGHT\` checks instruction stream
structure only, not full SPIRV-Tools semantic validation.

- **#75**, successful Windows Release, fixes #74's clang-cl bit-field
  logging compile error and incorporates fail-closed shader-used texture
  and vertex fetch guards. Gameplay visual improvement is unverified.
- **Candidate source commit \`5623eb1f\`** adds a single cross-thread,
  optional mutex around shader/PSO *host API creation calls* (default ON
  in Hulk TOML). This is a plausible crash containment measure, **not**
  an NVIDIA compiler root-cause fix. It serializes creation but does not
  prevent the driver's own asynchronous compilation or fix GPU rendering.
  At the time of this audit, there is **no #76 build/result** for it.

### Prioritized falsifiable acceptance checks

1. **P0: program identity.** The first packet where the GPU's pixel
   program differs from the NativeX tracker must carry a chronological
   per-packet program load and exact capture-time microcode identity;
   resolve the 58 frame-900 mismatches without making up shader words.
2. **P0: fetch identity.** Compare each used fetch slot against the PM4
   SET/LOAD chronology and XDK shadow at the same draw, tracking the
   \`1280.0/720.0\` writer through its original packet. Never replace a
   bad descriptor with unrelated data.
3. **P0: geometry/depth.** Compare 16-bit index byte order, vertex-buffer
   address and lifetime, 1x/4x target identity, depth and scissor at the
   first visibly damaged triangle. Reject incomplete calls, but count
   missing geometry separately from invalid geometry.
4. **P0: driver stability.** Check actual crash minidumps and shader
   translation statistics; use full semantic SPIR-V validation before
   claiming the driver is receiving valid bytecode.
5. **P1 after parity:** native EoR-integrated Options/Video/Controls/Audio
   menus, KB/M without XInput synthesis, physics-correct high FPS,
   campaign, saves, Title Update, native scene LOD and consumer UX.

No workflow duplication or private payload commits are part of this audit.

## Log192 / build #79: image restored, intermittent mesh dropout (12 November 2026)

**Evidence:** user screenshot of active city gameplay and 6,915-line private
`incredible_hulk_192.log` from 22:24–22:28 Kyiv. Contains no proprietary
game/shader bytes, owner paths, screenshots, saves or raw assets. Treat
single screenshots as static witnesses, not proof of temporal behavior.

- **Black-screen regression is fixed:** `HULK/PRESENT-FRAME-RECOVERY`
  reaches 6,600, Diligent/Vulkan reports 6,599 frames presented and 6,598
  completed. At frame 6,600 its queue fence is 13,208 / 13,205 and all
  600 recent frames were shown at actual guest swaps. Frame output is
  1280×720 guest → 2560×1440 host → 2560×1600 letterbox. No
  dynamic-heap-exhaustion in this trace. The black screen was caused by
  #77 discarding valid `Call::kPresent` under partial PM4 scan.
- **Major missing-geometry signal:** `HULK/INCOMPLETE-XDK-DRAW` reaches
  40,000 rejected DrawIndexedVertices (`call=0`, `was_draw=true`) by
  22:27:38, rather than a few exceptional calls. This aggressive fail-closed
  policy may contribute to whole Hulk body/scene patches disappearing.
  **Do not simply remove it**: before the guard, invalid GPU microcode and
  giant triangles appeared. Classify whether the rejected scan failed due to
  segment start/end mismatch, incomplete packet or guest memory before
  permitting a specific recovery. Source commit 294f6a77 adds bounded
  pointer/counter diagnostics WITHOUT changing the rejection policy.
- **Pixel and vertex program identity are still wrong:** frame900 1/1349
  matched calls differs; `SQ_INTERPOLATOR_CNTL` CPU 0x7f007f vs GPU
  0x3f003f. Frame2147 1/1908 differs, including
  `SQ_PROGRAM_CNTL` CPU 0x10001 vs GPU 0x1045090a and one VS program
  mismatch. Frame2157 only 1870/1919 matched calls exactly (97.4%),
  with **49 pixel-program mismatches**:
  CPU XXH3 e42552fce84cb8ac vs GPU 0dad3cb9972d38fd. The verifier
  found **no texture/vertex fetch mismatch in frame2157**, so a damaged
  texture at another time is not evidence of its cause in this frame.
- **GPU fetch/vertex issues:** logged invalid vertex descriptors
  (slots 95, then 0, types 0/1/2) and two invalid texture-draw warnings;
  `HULK/FETCH-WRITER` still records PM4 SET values 1280.0 and 720.0
  in fetch register words. One 27-dword vertex program
  `3A32665E608C57AA` caused an analysis access violation (caught; the
  game continued). These are faults in program/fetch provenance and must not
  be disguised by turning depth testing off or fabricating vertex data.
- **Depth/Z remains a hypothesis:** GPU frame900/2147 reports a few
  render-state mismatches; in frame2157 it reports **none**. Host exact
  640×360/4x and 1280×720/1x sample-grid color/depth storage aliases are
  logged for 2560×1440. They do not prove that every depth/stencil clear,
  compare, scissor and per-draw target is correct. Disabling Z-buffer is
  NOT an acceptable fix.
- **First-use host pipeline stalls:** in frame4200 summary, 71 draws were
  skipped while pipelines compiled, even though
  `native_x_renderer_hulk_wait_scene_pipelines=true` is configured.
  The existing *whole-frame* budget was 80 ms; after expiration later
  draws are still skipped. Source commit f00478ae raises the bounded shared
  budget to 200 ms for Hulk (never 3000 ms per individual draw) to reduce
  first-use holes. This may increase a few shader-loading stalls; it does
  not solve 49 incorrect pixel programs or 40k missing XDK calls.
- **Stable gameplay segment:** at frame6600 NativeX recorded 992 native
  Draw submissions, 9 clears and 28 resolves, no reported DrawInternal
  failures. This counts only accepted draws, so **must not** be used to
  deny the separate 40k earlier XDK rejects.
- **Other signals:** four-minute run ended normally; a UKENGLISH
  `INDEX.IND` path failed initially but fallback game resources loaded
  and the game reached city; this is not the observed flicker cause.

**Next gate:** after candidate source build, verify the first 16 and every
10,000th `HULK/INCOMPLETE-XDK-DRAW` reason by PM4 begin/scan_from/end
and nested-segment information; match *same* visible scene and shader
hashes against Xenos CP truth. Never mark Vulkan parity reached solely
because CI compiles or frame counters increase.


## Log 194: verification coverage gap, 2026-11-13

Audited against source main `04f76a9ddf20193f4492839ba67869fb4689997d`.
User reports transient colored/light flashes in the 3D menu and gameplay,
and a regular pixel/grid pattern at internal scale above 1. The two supplied
screenshots show the spatial pattern, but cannot establish the lifetime or
origin of the transient flashes. Neither symptom is fixed or release-qualified
by this audit.

The log confirms a 2x native scale: 1280x720 game surface -> 2560x1440 host
surface, presented at (0,80) within a 2560x1600 swapchain. This is a scaled
16:9 scene with letterboxing, not proof of correct per-pixel sampling or full
16:10 scene rendering. The actual host dimensions already increase, so changing
the output size alone cannot establish a fix for the grid.

All six verifier summaries report 100% matches while comparing **zero VS and
zero PS float constants**. The first summary matches 1274 draws and reports
1274 without shader analysis; subsequent matched counts are 1218, 899, 865,
1158 and 1618, likewise with zero float comparisons. The verifier returns early
when the SDK provides no shader usage map. Missing program hashes also skip
program comparisons. These summaries therefore do not exclude corrupted
lighting, transformation constants, shader selection, or depth behavior.

The existing observer patch now compares the full 256-vec4 register window
for a stage lacking usage metadata. When metadata exists, it still compares
only used constants (or the full window for dynamic addressing). The summary
states only that compared state has no mismatches, explicitly warns about
missing shader coverage, and counts missing analysis for either VS or PS.
Raw-window differences can include unused registers; they are evidence of
state divergence, not automatically evidence of a faulty rendered pixel.
This does not create shader analysis, verify shader semantics, or modify any
rendering pass, buffer, sampler, shader, depth test or resolution setting.

Validation: four observer patch tests, including a compiled C++ regression
using the actual old/new constant-selection loop. The old loop misses an
injected PS c17 divergence; the replacement detects it, respects known usage
and dynamic addressing, and handles a wrapping constant window. Complete
80-step source patch chain passes on NativeX 919371b5. No Actions dispatch,
Windows game execution or visual validation was performed for this change.

Remaining investigation must distinguish packet/constant state divergence
from sampling/resolve defects using the offending draw, shader and resource
contents. Existing log warnings include invalid texture/vertex draws and
incomplete XDK draw scans, but do not establish which operation produced a
visible flash. The sampler/resource/scale source review has not established
a new rendering fix. Do not replace missing evidence with arbitrary clamps,
removed effects, forced 1x resolution, or a claim of graphical parity.


## 13 November 2026 — never sample an allocated but unwritten resolve

A separate source-level defect exists in both `ResolvedTextures::FindNewest`
overloads. `GetOrCreate` allocates a texture with `last_resolve == 0`, while
`Resolve` may subsequently return before copying (for example, when its
requested destination slice has no view). Both address lookup overloads
previously returned this allocated image even though `Touch` had never recorded
a completed resolve. Texture sampling or presentation could consequently read
uninitialized GPU data instead of following the existing no-resolve path.

The existing resolved-resource patch now requires nonzero `last_resolve` at
both lookup returns. Valid completed resolves remain selectable. An invalidated
newest resolve still returns null; the change deliberately does not resurrect
an older version. No surfaces are cleared to invented colors, no effects or
depth tests are disabled, and scale is unchanged. Per-slice completeness and
whether every blit path reports actual success remain separate audit questions.

Four resource-guard tests pass. A compiled C++ regression executes the exact
old/new lookup-return expressions: the old expression exposes an unwritten
image, the replacement rejects it, accepts a completed image, and rejects
invalidated/null images. This proves the source defect and local fix, not
that it caused the transient color flashes in log 194 or fixes the scale>1
pixel grid. No Windows Actions compilation or game visual test was launched.


## 13 November evening — forward Resolve physical identity

Rebased pending fixes onto main 6cc4297, retaining its newer PM4 boundary,
BIN predication, depth/stencil and FPS work. Review found an asymmetry in
`patch_native_x_hulk_shared_write_order.py`: reverse alias selection compares
physical image identities and latest physical write epochs, while forward
`FindMsaaAlias` consumption still compared `HostTarget*` and per-view stamps.
Two distinct guest descriptors sharing one ITexture could therefore satisfy
the forward alias test and enter coordinate reinterpretation for an image
already represented by the source. A separate candidate could also appear
newer than a stale source view although older than its actual physical image.

Both directions now require a distinct physical image and a newer physical
write epoch. A compiled C++ test executes the exact old/new forward condition:
old code selects a same-image alias, fixed code rejects it, rejects an older
distinct image, accepts a newer distinct image even with a stale descriptor
stamp, and handles null/self candidates. No guest addresses, shader bytes,
scale settings or effects were changed. This is a tested source fix, not
runtime proof of elimination of log198's flashes or the scale>1 grid.

The current-main public preflight also failed before renderer preparation:
`test_hulk_us_gpu_defaults.py` still required gpu_guest_fps_limit=30 after
main changed configuration and SDK default to 0 to preserve original guest
pacing. The test now requires that intentional zero default in BOTH places;
it does not enable an FPS unlock or assert corrected high-FPS physics.


## Log 200 and cross-chat chronology — 13 November 2026

Times below are Europe/Kyiv (UTC+02). GitHub run and release asset metadata
were queried directly; conversation summaries alone were not treated as CI
results. Log timestamps have no explicit timezone; they are interpreted as
the user's local clock, consistent with the reported run sequence.

| Event | Local time | Verified scope |
| --- | --- | --- |
| #93, source 3baba02ddffa | ZIP created 16:41:01 | Actual 55,178,493-byte ZIP; graphics remained faulty |
| User requests final work then Actions in another project chat | 21:15:05 | Retrieved conversation instruction, not a successful build |
| #94, source 890a9287 | 21:22:04–21:22:57 | FAILED preflight; no successful Windows build |
| #95, source 2775a14efd50 | 21:25:23–21:47:52 | SUCCESS; ZIP created 21:47:43, 55,179,922 bytes |
| Later FPS implementation/default/UI source | 21:33–21:38 | After #95 source checkout; not included in #95 |
| Log 200 | 21:55:05.926–21:57:48.401 | User's latest archive run; normal window-close shutdown recorded |
| Resolve/image identity and save enumeration fix 2cb2f6f | 22:04:53 | After log200, source-tested only, not in that archive |

Run95: https://github.com/AD010/Hulk-NativePC-Source/actions/runs/43492048591
Its private asset is `Hulk-NativePC-Vulkan-PresentFix-2775a14efd50.zip` under
the existing hulk-cloud release. The log has no embedded source SHA: association
with #95 is consistent with the user's latest-archive description and timing,
not a cryptographic identification of the executable they launched. Do not
relabel log200 as a test of 2cb2f6f or of the later FPS limiter.

### What the new log actually establishes

Native scene target is 2560x1440 for 2560x1600 output at scale2. Sampled
verification results are:

| Frame | Matched draws | No differences in compared state | PS program mismatches | VS float evidence | Texture-fetch evidence |
| --- | ---: | ---: | ---: | --- | --- |
| 900 | 11 | 11 | 0 | 2816 vec4 comparisons, none differ | none |
| 1119 | 1847 | 1594 | 0 | 472832 comparisons; c21..c36 each differ on253 executions,108 more entries omitted | slots3/4 each59 |
| 1790 | 1555 | 1282 | 0 | 398080 comparisons;16 entries shown,56 omitted; shown entries each273 | none |
| 1800 | 1554 | 1379 | 0 | 397824 comparisons; c21..c36 each175,108 more entries omitted | slots3/4 each35 |

All these executions lack VS or PS usage analysis, so float comparison is of
raw windows. A mismatch in an unused register is not proof of a bad vertex.
Do not sum per-register counts into a number of broken draws. Do not infer
that all previous50 PS mismatches are fixed: different frames and scenes were
sampled, zero program hashes can be unverified, and runtime appearance still
fails the user's acceptance criterion.

The highest logged incomplete-XDK rejection counter is20000 (a lower bound,
not the number of detailed warning lines). The process closes normally in
this run. Last sampled frame6000 has590/590 drawn,0 failed/compilation skips,
16.67ms average frame interval. Earlier gameplay intervals are about33ms;
this does not prove60FPS gameplay with corrected physics. Large no-writable-RT
helper counters must not be called discarded scene meshes without attribution.

The audit tool previously retained match rates and PS mismatch counts but
lost float/fetch detail. It now retains per-sample coverage, compared counts,
reported register/slot entries and explicit truncation counts. Missing reports
remain unknown, not zero. This is an analysis correction, not a runtime fix.

### Remaining acceptance gates

- Graphics: resolve the actual state divergence and incomplete XDK streams;
  identify whether divergent constants are read by the affected VS. Validate
  shader semantics, depth/stencil, geometry, materials, lighting, shadows,
  water, intro/title/pause/HUD and scale>1 by actual frame evidence.
- Timing: preserve baseline by default; measure60+ gameplay and separately
  prove physics/timestep behavior. A VdSwap cap is not a physics patch.
- EU TU1: USA semantic match and tested water-entry change remain incomplete;
  EU binary replacement is not a valid transfer.
- Saves: native Documents namespace and guarded import exist but remain opt-in;
  game load/save and atomic payload writes are not established. Metadata safety
  is not whole-save transactional safety; retail-PC format differs.
- Input/audio/window/UI: verify direct camera ownership, keyboard/mouse without
  virtual-controller dependency, native menu/audio lifecycle, Alt+Tab and175%DPI.
- Release: keep the existing single workflow. No new Actions was dispatched
  during this audit; prior user-authorized #95 is already complete. Compilation
  does not close any visual/gameplay gate. Never call the latest ZIP final.
