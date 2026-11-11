# NativeX index ownership and upload ordering — 2026-11-11

## Continuation: index conversion cache invalidation

Audited current main `fda4e4623bd96a71ee92c312ead456008ab8438f` after
the owner's manual-only release gate. No new Windows run is started here.
Build 59 was cancelled by a subsequent run. Build 62 completed successfully
at `e06ec7ce8a52df0a7d52fddc2de55b381e4df4c3`; its private ZIP is
`Hulk-NativePC-Vulkan-PresentFix-e06ec7ce8a52.zip` (55,150,016 bytes).
It does NOT include later main changes or the cache fix described below.

Latest inspected gameplay evidence is now `incredible_hulk_176.log`:
4,815 lines, 561 errors, 244 oversized 1D texture errors, 248 tiled 1D
texture errors, and 63 unsupported primitive errors. No shader-analysis
failure line appears in this log. This does not establish shader correctness
or the cause of the reported crash. The log still presents a 2560x1440 host
image into a 2560x1600 swapchain. Neither build 62 nor current main was
validated by this older gameplay log.

The ReXGlue `recomp` branch's `src/graphics/primitive_processor.cpp`, blob
`d49e9c35a799a283df496d21de6fdca72a5e60ea`, tests cache overlap using
`entry.base < write.end && entry.end > write.end`. This misses exact range
rewrites, writes containing the entire entry, and some right-edge overlaps.
The corrected second comparison is `entry.end > write.start`, preserving
the existing bucket traversal, locking and eviction implementation.

Example: cached indices [100,120), write [100,120). The old predicate keeps
the stale converted indices; the corrected predicate invalidates them.
Adjacent, non-overlapping writes still retain the cache. No blanket cache
disable or guest memory modification is used.

Validation compiles the actual source's overlap predicates and checks them
against an independent byte-enumeration oracle for all 18,496 pairs of
nonempty intervals in a 16-byte domain, plus physical-buffer boundary cases.
Tests reproduce the old false-negative and pass after patching. They also
check idempotence and refuse unknown/partial SDK layouts. They do not execute
the full cache's locking/bucket implementation or prove GPU image parity.
The existing Windows builder invokes this check against its actual SDK
before installing the narrow fix. The workflow remains manual-only.

## Continuation: CPU consumers of captured index versions

Source audit of main `6c33bd0` found FOUR CPU reads of live physical indices:
primitive conversion, 16-bit restart replacement, 32-bit restart replacement,
and 32-to-24-bit normalization. The GPU mirror restore cannot affect these
CPU reads. A correct cache-overlap predicate alone cannot fix that lifetime.

The existing SDK patch now adds `ProcessCapturedIndices` while preserving
`Process` for ordinary live-memory clients. The new entry point validates
source, count, address, length and physical bounds, copies byte-packed command
data into naturally aligned storage, and passes it to all four CPU reads.
It invalidates only that physical range's conversion cache before and after
processing (including failure exits). Cached results keyed to live memory
cannot be consumed by an older captured draw or retained as its new version.
No guest bytes are rolled back. Snapshot conversions are recomputed; the
performance cost is not yet measured. Other ranges retain their caches.

The existing NativeX capture patch also routes DrawInternal through this
entry point when index bytes are present in the command. Ordered copy
composition matches the GPU restore order, including overlapping copies.
A partially covered index range is rejected, never completed with newer
live bytes. Draws without index copies retain the existing Process path.
Thus this does not claim to fix missing vertex/texture ownership or draws
whose capture itself was incorrect.

Validation: compile and execute the actual NativeX composition helper,
checking 1,296 two-range combinations against a per-byte last-writer oracle,
plus holes, null data, out-of-range values and absent copies. Compile and
execute the actual SDK entry wrappers with cache/converter test doubles,
checking natural alignment, two versions at one address, invalid arguments,
and cleanup on both success and failure. Source transforms verify all four
consumer anchors and idempotence on the real fetched SDK header/source.
These are CPU regression tests; they do not compile the complete SDK,
exercise its cache mutex/bucket implementation, or establish any pixel result.

Validation completed: 3 SDK regression tests, 8 NativeX snapshot tests,
and both complete 56-step chains (919371b5 and bf965e95) passed. Public
source audit and git diff whitespace checks passed. No Windows build or
game replay is claimed by this source-only change.

Baseline: source main `99368e184979b1e00cb51c74af8049c991bd3653`;
NativeX production pin `919371b5bdae0c9c4c7120ca321f5a4d72b7e78e`.
Latest available owner gameplay log inspected: `incredible_hulk_175.log`
(10 November, 21:38–21:41 as recorded). No game bytes are included here.

## Evidence and scope

The log records invalid texture-fetch slots 1/2 consumed by pixel shader
DBEE5B430D8DB5F7, including screen-size float values written by SET packets;
1D texture errors; unsupported primitive 49; and at least 730,000 ambiguous
synthetic surface bindings. The 2560x1600 swapchain presents a 2560x1440 host
image. Neither true 16:10 scene rendering nor image correctness is established.
The log predates build 57 and cannot validate that build or this change.

Source inspection found two independent index ownership problems:

1. CaptureDraw snapshots DMA indices only for synthetic packet draws.
   Ordinary DrawIndexedVertices commands are also deferred, but do not own
   their index bytes. Extend the existing bounded snapshot helper to this
   call, using its actual initiator and effective aligned DMA range.
2. DrawInternal restores captured bytes with WriteIfChanged, which invalidates
   the corresponding mirror pages. UploadAhead then requests queued draws'
   ranges and can upload the current guest bytes over that restoration.
   Run speculative uploads first, then restore the current draw's snapshots.
   Preserve the existing cache invalidation and PrepareForRead barrier.

No guessed XDK addresses, altered texture values, disabled shadows, fake
indices, new configuration copies, or new workflows are introduced.

## Validation

The regression compiles the production C++ snapshot helper (only the guest
address arena is mapped to a small host array). It verifies aligned capture,
independent queued snapshots after guest reuse, short DMA rejection and bounds.
It executes the old and patched restoration blocks against an invalidating
mirror: old order produces the later draw's byte 9; patched order retains the
current draw's byte 3. This is a CPU behavioral test, not GPU replay.

The existing 52-step patch chain validates both supported NativeX revisions.
The existing Windows workflow is used for Release compilation and packaging.
CI status and the resulting private ZIP must be checked separately.

## Remaining gates

- No on-hardware game run or Vulkan pixel-history test was performed here.
- Historical audit finding (addressed by the CPU snapshot continuation above):
  restoring the GPU mirror does not prove that CPU fan/quad/loop conversion
  consumed captured indices. Vertex-buffer lifetime is also a separate gate.
- Invalid fetch writers, shader failures, clear/depth/resolve parity and
  ambiguous surface bindings are not claimed fixed by these index changes.
- Native camera, physics-correct 60+ FPS, audio/runtime replacement, removal
  of the Xenos synchronization dependency, Alt+Tab and DPI still need their
  respective implementation/validation gates.
- Documents/SEGA/The Incredible Hulk is currently a path resolver/policy;
  active save serialization and safe migration are not implemented. Existing
  saves are untouched by this change.

## Latest gameplay evidence: incredible_hulk_178.log

Read all 3,172 lines, 21:13:59–21:15:02 on 11 November as recorded.
The log ends with window closing, Execution complete and Title terminated;
it does not establish an unhandled crash. In the post-initialization portion
(lines 101–3172), there are 181 error lines including 87 oversized 1D fetches
and 87 tiled 1D fetches. Fetch-writer records still place screen-size float
bits `44A00000`/`44340000` in slots 1/2 consumed by `DBEE5B430D8DB5F7`.
These are observations, not permission to replace those registers with
invented texture descriptors or to suppress the postprocess.

Verifier samples report 11/11, 1164/1271 and 1341/1345 matched draw states;
the latter two still have 43 unmatched GPU packets. The 91.6% sample includes
pixel-program mismatches; the 99.7% sample includes SQ_PROGRAM_CNTL and
SQ_INTERPOLATOR_CNTL mismatches. Neither sample is pixel parity. At frame
600 the renderer reports 577 draws skipped for shader translation and 823
for pipeline compilation since startup; current main subsequently added
bounded scene waits. That source change is not validated by this older log,
and a bounded wait can still expire without drawing. No claim of corrected
startup screens, menu, city, water, 16:10 scene projection or 60+ FPS is made.

Per the latest owner instruction, no Actions dispatch, game build or new
release is authorized without another command. Only source/test work here.
