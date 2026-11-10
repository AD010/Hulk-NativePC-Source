#!/usr/bin/env python3
"""Snapshot real PM4 DMA index bytes before Hulk's guest command ring is reused.

Synthetic ordered PM4 draws are captured on the game thread but rasterized
on the NativeX Vulkan thread. Unlike UP/inline XDK draws, these packets did
not preserve their DMA index bytes, so later ring writes can corrupt indices.
Only the packet's own DMA_BASE/SIZE are copied, bounded by guest physical
memory and by a conservative 1 MiB per-command cap. No fake indices, no
fabricated shaders, no Xenos host rendering.
"""
from pathlib import Path
import os
import subprocess
import sys

PIN=os.environ.get("HULK_NATIVEX_SOURCE_SHA","bf965e952c98e050753e91d08f32685a9f754b12")
ANCHOR="void CaptureDraw(const TrackedCall& tracked, StateTracker& tracker, const uint8_t* base,\n                 const std::array<GuestSurface, 5>* packet_surfaces = nullptr) {"
ADDITION="""// HULK/PM4-DMA-SNAPSHOT: the graphics thread runs after this guest
// ring-buffer segment may have been reused by the game. Snapshot only
// the DMA indices named by THIS draw's PM4 initiator and DMA registers.
bool CapturePacketDmaIndices(const StateTracker& tracker, const uint8_t* base, Command& c) {
  namespace rg = rex::graphics;
  uint32_t initiator = 0, dma_base = 0, dma_size = 0;
  // A synthetic packet must not rasterize with a missing/stale DMA index
  // buffer. Log 173 observed 99/354/1248 indices requested while
  // VGT_DMA_SIZE was zero; previously this was still passed to DrawPath.
  auto reject = [&](const char* why) -> bool {
    static std::atomic<uint64_t> rejected{0};
    const uint64_t n = ++rejected;
    if (n <= 12 || n % 10000 == 0)
      REXLOG_WARN("[HULK/PM4-DMA-REJECT] #{} {} initiator={:#x} base={:#x} size={:#x}",
                  n, why, initiator, dma_base, dma_size);
    return false;
  };
  if (!tracker.LastWrite(rg::XE_GPU_REG_VGT_DRAW_INITIATOR, initiator))
    return reject("no draw initiator");
  if (((initiator >> 6) & 3u) != 0)
    return true;  // Auto-indexed packet; there is no DMA index buffer.
  if (!tracker.LastWrite(rg::XE_GPU_REG_VGT_DMA_BASE, dma_base) ||
      !tracker.LastWrite(rg::XE_GPU_REG_VGT_DMA_SIZE, dma_size))
    return reject("missing DMA BASE/SIZE");
  const uint32_t requested_indices = initiator >> 16;
  const uint32_t available_indices = dma_size & 0x00FFFFFFu;
  // Never silently truncate requested indices: DrawInternal would still
  // interpret the original count and read outside the captured bytes.
  if (!requested_indices || requested_indices > available_indices)
    return reject("DMA index count missing/short");
  const uint32_t indices = requested_indices;
  const uint32_t stride = ((initiator >> 11) & 1u) ? 4u : 2u;
  const uint64_t bytes = uint64_t(indices) * stride;
  const uint32_t physical = dma_base & 0x1FFFFFFFu;
  constexpr uint64_t kGuestPhysicalBytes = 0x20000000ull;
  constexpr uint64_t kMaxSnapshotBytes = 1ull << 20;
  if (!bytes || bytes > kMaxSnapshotBytes ||
      uint64_t(physical) + bytes > kGuestPhysicalBytes ||
      c.copy_count >= c.copies.size())
    return reject("DMA address/length invalid");
  const uint8_t* source = base + (0xA0000000u | physical);
  c.copies[c.copy_count++] = {dma_base, uint32_t(c.data.size()), uint32_t(bytes)};
  c.data.insert(c.data.end(), source, source + bytes);
  static std::atomic<uint64_t> snapped{0};
  const uint64_t n = ++snapped;
  if (n <= 8 || n % 250000 == 0)
    REXLOG_INFO("[HULK/PM4-DMA-SNAPSHOT] #{} {} indices snapshotted ({}/{} available/requested)",
                n, indices, available_indices, requested_indices);
  return true;
}

"""
CALL_ANCHOR="""  c.programs[1] = tracker.pixel_program_ptr();
  if ((Info(tracked.call).flags & kInlineEnd) || tracked.call == Call::kDrawVerticesUP) {"""
CALL_NEW="""  c.programs[1] = tracker.pixel_program_ptr();
  if (packet_surfaces && !CapturePacketDmaIndices(tracker, base, c)) return;
  if ((Info(tracked.call).flags & kInlineEnd) || tracked.call == Call::kDrawVerticesUP) {"""

def patch_text(source):
    present=ADDITION in source and CALL_NEW in source
    if present:return source,False
    if ADDITION in source or CALL_NEW in source:
        raise ValueError("Partially installed Hulk PM4 DMA snapshot")
    if source.count(ANCHOR)!=1 or source.count(CALL_ANCHOR)!=1:
        raise ValueError("NativeXRenderer packet capture/inline DMA anchor drift")
    source=source.replace(ANCHOR,ADDITION+ANCHOR,1)
    source=source.replace(CALL_ANCHOR,CALL_NEW,1)
    return source,True

def main():
    if len(sys.argv)!=2:
        raise SystemExit("Usage: patch_native_x_hulk_packet_dma_snapshot.py <NativeX checkout>")
    root=Path(sys.argv[1]).resolve()
    actual=subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if actual!=PIN:
        raise ValueError("Unexpected NativeX checkout SHA: "+actual)
    path=root/"src/renderer.cpp"
    result,changed=patch_text(path.read_text(encoding="utf-8"))
    if changed:path.write_text(result,encoding="utf-8",newline="\n")
    print("Hulk PM4 per-packet bounded index snapshot:", "patched" if changed else "already installed")
if __name__=="__main__":main()
