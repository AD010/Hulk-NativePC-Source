# NativeX index ownership and upload ordering — 2026-11-11

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
- The CPU primitive conversion path still requires its own ownership audit:
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
