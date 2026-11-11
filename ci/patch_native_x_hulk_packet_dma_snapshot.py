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
  // HULK/PM4-PRIMITIVE-SOURCE: Xenos VGT_DRAW_INITIATOR[5:0]
  // names only primitive types 1..8, 12..22 (Xenia xenos::PrimitiveType).
  // Build 56 repeatedly submitted type 49, which is not any Xenos
  // primitive: never let stale register bits reach PrimitiveProcessor.
  const uint32_t primitive = initiator & 0x3Fu;
  if (!((primitive >= 1u && primitive <= 8u) ||
        (primitive >= 12u && primitive <= 22u)))
    return reject("invalid Xenos primitive type");
  const uint32_t source_select = (initiator >> 6) & 3u;
  if (source_select == 2u)
    return true;  // Xenos kAutoIndex: no DMA index buffer is required.
  // kImmediate embeds indices in the PM4 payload; this snapshot path has
  // not captured them and MUST NOT reinterpret stale DMA registers.
  // Source 3 is reserved by the Xenos source-select enumeration.
  if (source_select != 0u)
    return reject("PM4 immediate/reserved indices not captured");
  // HULK/PM4-PERSISTENT-DMA: Xenos DMA BASE/SIZE registers persist between
  // draw packets. Gather() has already applied this packet's ordered updates
  // to the GPU register image; LastWrite() only sees this packet span, and
  // would reject a draw that legally reuses an earlier DMA binding.
  dma_base = tracker.HulkRegisterAtDraw(rg::XE_GPU_REG_VGT_DMA_BASE);
  dma_size = tracker.HulkRegisterAtDraw(rg::XE_GPU_REG_VGT_DMA_SIZE);
  const uint32_t requested_indices = initiator >> 16;
  const uint32_t available_indices = dma_size & 0x00FFFFFFu;
  // Never silently truncate requested indices: DrawInternal would still
  // interpret the original count and read outside the captured bytes.
  if (!requested_indices || requested_indices > available_indices)
    return reject("DMA index count missing/short");
  const uint32_t indices = requested_indices;
  const uint32_t stride = ((initiator >> 11) & 1u) ? 4u : 2u;
  const uint64_t bytes = uint64_t(indices) * stride;
  // HULK/PM4-DMA-ADDRESS-PARITY: ReXGlue's PrimitiveProcessor uses
  // reg(VGT_DMA_BASE) & ~(index_stride - 1) as its physical index address.
  // The previous snapshot read from (dma_base & 0x1FFFFFFF) unaligned,
  // and submitted a Copy at raw dma_base. Those can differ from the
  // exact byte range that Vulkan's index processor consumes.
  // Do NOT silently drop high address bits: PrimitiveProcessor itself
  // rejects anything outside the physical 512 MiB buffer.
  const uint32_t physical = dma_base & ~(stride - 1u);
  constexpr uint64_t kGuestPhysicalBytes = 0x20000000ull;
  constexpr uint64_t kMaxSnapshotBytes = 1ull << 20;
  if (!bytes || bytes > kMaxSnapshotBytes ||
      uint64_t(physical) + bytes > kGuestPhysicalBytes ||
      c.copy_count >= c.copies.size())
    return reject("DMA address/length invalid");
  const uint8_t* source = base + (0xA0000000u | physical);
  // The GPU mirror and PrimitiveProcessor must receive the SAME aligned
  // address; physical is within 512 MiB after the bounds check above.
  c.copies[c.copy_count++] = {physical, uint32_t(c.data.size()), uint32_t(bytes)};
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
  // HULK/DIRECT-DMA-SNAPSHOT: ordinary XDK indexed draws are queued too.
  // Their indices need the same owned lifetime as packet-replayed draws.
  if ((packet_surfaces || tracked.call == Call::kDrawIndexedVertices) &&
      !CapturePacketDmaIndices(tracker, base, c)) {
    // HULK/PM4-DMA-CANCEL: Capture::~Capture() unconditionally Publish()es
    // the command, EVEN on early return. Returning without changing its
    // kDraw type still runs that rejected draw on the Vulkan thread.
    // kNone executes only c.registers (preserving ordered PM4 GPU state);
    // it cannot enter ExecuteDraw or read the missing guest DMA indices.
    c.type = Command::Type::kNone;
    return;
  }
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

RESTORE = """  // An inline draw's data, if the game reused its memory since the draw was captured (D71).
  for (const Copy& copy : copies) {
    if (memory_->WriteIfChanged(context, copy.address, copy.data, copy.size)) ++stats_.inline_restored;
  }
"""
AHEAD = "  if (upload_ahead_ && memory_->copies() != copies_at_start) upload_ahead_();\n"
RESTORE_AFTER = """  // HULK/DMA-RESTORE-AFTER-AHEAD: restoring a captured range invalidates
  // its guest mirror pages. UploadAhead may request the same pages for a
  // later draw and replace this draw's saved bytes with newer guest data.
  // Finish speculative uploads before restoring the current command.
  for (const Copy& copy : copies) {
    if (memory_->WriteIfChanged(context, copy.address, copy.data, copy.size)) ++stats_.inline_restored;
  }
"""

def patch_draw(source):
    if RESTORE_AFTER in source:
        if RESTORE in source or source.count(RESTORE_AFTER) != 1:
            raise ValueError("Partial DMA restore ordering patch")
        return source, False
    if source.count(RESTORE) != 1 or source.count(AHEAD) != 1:
        raise ValueError("NativeX DMA restore/upload-ahead anchor drift")
    return source.replace(RESTORE, "", 1).replace(AHEAD, AHEAD + RESTORE_AFTER, 1), True

def main():
    if len(sys.argv)!=2:
        raise SystemExit("Usage: patch_native_x_hulk_packet_dma_snapshot.py <NativeX checkout>")
    root=Path(sys.argv[1]).resolve()
    actual=subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if actual!=PIN:
        raise ValueError("Unexpected NativeX checkout SHA: "+actual)
    edits=[]
    for name, action in (("src/renderer.cpp", patch_text), ("src/draw.cpp", patch_draw)):
        path=root/name
        result,changed=action(path.read_text(encoding="utf-8"))
        edits.append((path,result,changed))
    for path,result,changed in edits:
        if changed:path.write_text(result,encoding="utf-8",newline="\n")
    changed=any(edit[2] for edit in edits)
    print("Hulk PM4 per-packet bounded index snapshot:", "patched" if changed else "already installed")
if __name__=="__main__":main()
