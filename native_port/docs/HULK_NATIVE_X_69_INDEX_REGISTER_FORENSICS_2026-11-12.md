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
