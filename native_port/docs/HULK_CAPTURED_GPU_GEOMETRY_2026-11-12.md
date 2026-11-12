# Captured geometry: CPU equality is not GPU mirror equality

Status: source-level defect reproduced and corrected; game-image parity and
performance remain unverified. No Windows build or Actions run performed.

NativeX GuestMemory::WriteIfChanged compared captured command bytes against
live guest CPU memory. If equal, it skipped uploading those bytes to the separate
GPU mirror. That comparison cannot establish what bytes the GPU mirror contains.

A deterministic counterexample uses two ordered overlapping copies:

- Copy A writes bytes 1,2,3,4,5,6,7,8 at offset 16.
- Copy B writes bytes 9,10,11,12 at offset 18, matching live CPU memory there.
- CPU index composition correctly produces 1,2,9,10,11,12,7,8.
- The old GPU restoration writes A, then skips B because B equals live memory.
  GPU indices remain 1,2,3,4,5,6,7,8. CPU primitive conversion and GPU fetch now
  consume different data despite using the same captured command.

The existing DMA snapshot patch now removes this invalid early-out in the real
GuestMemory method. It writes each valid captured range in order after upload-ahead
and retains the existing resource transitions and page/hash invalidation. Invalid,
empty and null ranges are rejected. It never modifies guest memory or replaces
indices, shaders, triangles, textures or target sizes.

The regression compiles the exact original and transformed method bodies with a
small mock GPU buffer. The original exits with the expected mismatch; the repaired
method passes overlapping writes, independently changed GPU bytes, byte counters,
page invalidation, and range/null rejection. Nine DMA snapshot tests pass. Both
supported NativeX pins pass all 72 source patch-chain steps. Reapplication to the
current production renderer is idempotent.

This is an invariant violation relevant to corrupted geometry; it is not proof
that all user-visible colored triangles originate here. Hooked vertex streams,
packet-time state/shader identity and resource synchronization still need image
and capture verification. The separate NVIDIA compiler crash is unresolved.

Cost: captured ranges are now always uploaded, so this may increase transfer calls
and render-pass breaks. Any later deduplication must compare against a verified
GPU-mirror version including all intervening writes, not current guest bytes.
No 60 FPS claim is made. Internal render scale and resolve filtering are unchanged;
the higher-than-1x pixelation requirement remains an independent validation gate.
