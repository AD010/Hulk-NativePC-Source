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
