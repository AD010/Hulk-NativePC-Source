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
bool CapturePacketDmaIndices(const StateTracker& tracker, const uint8_t* base, Command& c,
                             bool packet_draw) {
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
  // HULK/XDK-PERSISTENT-INITIATOR: ordinary DrawIndexedVertices may reuse a
  // legal initiator set by a prior PM4 packet; LastWrite() only searches the
  // current scan and previously dropped legitimate hooked XDK geometry
  // (log 177 "no draw initiator"). Still require a packet-local initiator
  // for synthetic packet-replayed draws: no guessed PM4 commands.
  if (!tracker.LastWrite(rg::XE_GPU_REG_VGT_DRAW_INITIATOR, initiator)) {
    if (packet_draw)
      return reject("synthetic packet lacks draw initiator");
    initiator = tracker.HulkRegisterAtDraw(rg::XE_GPU_REG_VGT_DRAW_INITIATOR);
    if (!initiator)
      return reject("no effective XDK draw initiator");
  }
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
  // HULK/PM4-DMA-INDEX-ENDIAN: for 16-bit indices the host PrimitiveProcessor
  // currently normalizes Xenos k8in32 (mode 2) to k8in16, and k16in32
  // (mode 3) to kNone. Both require the two 16-bit halves of each
  // 32-bit physical word to exchange places FIRST. Make this exchange in
  // this draw's captured OWNED bytes, not in the game's guest memory.
  const uint32_t swap_mode = dma_size >> 30;
  const bool pair_swap = stride == 2u && swap_mode >= 2u;
  // HULK/PM4-DMA-ADDRESS-PARITY: ReXGlue's PrimitiveProcessor uses
  // reg(VGT_DMA_BASE) & ~(index_stride - 1) as its physical index address.
  // The previous snapshot read from (dma_base & 0x1FFFFFFF) unaligned,
  // and submitted a Copy at raw dma_base. Those can differ from the
  // exact byte range that Vulkan's index processor consumes.
  // Do NOT silently drop high address bits: PrimitiveProcessor itself
  // rejects anything outside the physical 512 MiB buffer.
  const uint32_t physical = dma_base & ~(stride - 1u);
  // A 16-in-32 swap accesses a WHOLE 32-bit word even when the index base
  // starts at the high half or the index count is odd. Snapshot that exact
  // surrounding word, so adjacent 16-bit indices are not fabricated.
  const uint32_t capture_base = pair_swap ? (physical & ~3u) : physical;
  const uint64_t capture_bytes = pair_swap
      ? ((uint64_t(physical - capture_base) + bytes + 3ull) & ~3ull)
      : bytes;
  constexpr uint64_t kGuestPhysicalBytes = 0x20000000ull;
  constexpr uint64_t kMaxSnapshotBytes = 1ull << 20;
  if (!bytes || capture_bytes > kMaxSnapshotBytes ||
      uint64_t(capture_base) + capture_bytes > kGuestPhysicalBytes ||
      c.copy_count >= c.copies.size())
    return reject("DMA address/length invalid");
  const uint8_t* source = base + (0xA0000000u | capture_base);
  // The GPU mirror and CPU PrimitiveProcessor must get the same captured
  // byte order for this draw. Preserve every original byte except for the
  // necessary 16-bit-half exchange and restore it per draw.
  const uint32_t offset = uint32_t(c.data.size());
  c.copies[c.copy_count++] = {capture_base, offset, uint32_t(capture_bytes)};
  c.data.insert(c.data.end(), source, source + capture_bytes);
  if (pair_swap) {
    for (uint32_t i = 0; i < capture_bytes; i += 4) {
      std::swap(c.data[offset + i], c.data[offset + i + 2]);
      std::swap(c.data[offset + i + 1], c.data[offset + i + 3]);
    }
  }
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
      !CapturePacketDmaIndices(tracker, base, c, packet_surfaces != nullptr)) {
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

# HULK/INLINE-SNAPSHOT-BOUNDS: the old inline copy silently truncated
# invalid vertex/index ranges or aliased high GPU addresses to unrelated
# guest pages, then still published the draw (destructor always publishes).
# Retain the exact original fetch and DMA packet logic for valid ranges.
INLINE_OLD = "void CaptureInlineData(const StateTracker& tracker, const uint8_t* base, Command& c) {\n  namespace rg = rex::graphics;\n  auto copy = [&](uint32_t address, uint32_t size) {\n    constexpr uint32_t kMaxSize = 1u << 20;\n    if (!size || size > kMaxSize || c.copy_count >= c.copies.size()) return;\n    const uint8_t* source = base + (0xA0000000u | (address & 0x1FFFFFFFu));\n    c.copies[c.copy_count++] = {address, uint32_t(c.data.size()), size};\n    c.data.insert(c.data.end(), source, source + size);\n  };\n  uint32_t fetch0, fetch1;\n  if (tracker.LastWrite(rg::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + 31 * 6 + 4, fetch0) &&\n      tracker.LastWrite(rg::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + 31 * 6 + 5, fetch1) &&\n      (fetch0 & 3) == uint32_t(rg::xenos::FetchConstantType::kVertex)) {\n    copy(fetch0 & ~3u, ((fetch1 >> 2) & 0xFFFFFF) * 4);\n  }\n  uint32_t initiator, dma_base;\n  if (tracker.LastWrite(rg::XE_GPU_REG_VGT_DRAW_INITIATOR, initiator) && ((initiator >> 6) & 3) == 0 &&\n      tracker.LastWrite(rg::XE_GPU_REG_VGT_DMA_BASE, dma_base)) {  // DMA indices: 16 or 32 bits each\n    copy(dma_base, (initiator >> 16) * ((initiator >> 11) & 1 ? 4 : 2));\n  }\n}"
INLINE_NEW = "bool CaptureInlineData(const StateTracker& tracker, const uint8_t* base, Command& c) {\n  namespace rg = rex::graphics;\n  auto copy = [&](uint32_t address, uint32_t size) -> bool {\n    constexpr uint32_t kMaxSize = 1u << 20;\n    // HULK/INLINE-SNAPSHOT-BOUNDS: do not wrap a guest GPU address into\n    // some unrelated low physical page. Fail the draw before queueing it.\n    constexpr uint64_t kGuestPhysicalBytes = 0x20000000ull;\n    if (!size || size > kMaxSize || c.copy_count >= c.copies.size() ||\n        uint64_t(address) + size > kGuestPhysicalBytes) return false;\n    const uint8_t* source = base + (0xA0000000u | address);\n    c.copies[c.copy_count++] = {address, uint32_t(c.data.size()), size};\n    c.data.insert(c.data.end(), source, source + size);\n    return true;\n  };\n  uint32_t fetch0, fetch1;\n  if (tracker.LastWrite(rg::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + 31 * 6 + 4, fetch0) &&\n      tracker.LastWrite(rg::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + 31 * 6 + 5, fetch1) &&\n      (fetch0 & 3) == uint32_t(rg::xenos::FetchConstantType::kVertex)) {\n    const uint32_t inline_bytes = ((fetch1 >> 2) & 0xFFFFFFu) * 4u;\n    // Preserve the original valid zero-length no-copy behavior.\n    if (inline_bytes && !copy(fetch0 & ~3u, inline_bytes)) return false;\n  }\n  uint32_t initiator, dma_base;\n  if (tracker.LastWrite(rg::XE_GPU_REG_VGT_DRAW_INITIATOR, initiator) && ((initiator >> 6) & 3) == 0 &&\n      tracker.LastWrite(rg::XE_GPU_REG_VGT_DMA_BASE, dma_base)) {  // DMA indices: 16 or 32 bits each\n    const uint32_t dma_bytes = (initiator >> 16) * ((initiator >> 11) & 1 ? 4u : 2u);\n    if (dma_bytes && !copy(dma_base, dma_bytes)) return false;\n  }\n  return true;\n}"
INLINE_CALL_OLD = "    CaptureInlineData(tracker, base, c);"
INLINE_CALL_NEW = "    if (!CaptureInlineData(tracker, base, c)) {\n      // HULK/INLINE-SNAPSHOT-CANCEL: Capture destructor publishes on return.\n      // Preserve captured GPU register writes, but never execute a Draw\n      // whose inline indices/vertex stream were not captured safely.\n      c.type = Command::Type::kNone;\n      return;\n    }"


def patch_inline_capture(source):
    complete = INLINE_NEW in source and INLINE_CALL_NEW in source
    if complete:
        return source, False
    if INLINE_NEW in source or INLINE_CALL_NEW in source or "HULK/INLINE-SNAPSHOT-BOUNDS" in source:
        raise ValueError("Partially installed Hulk inline snapshot bounds/cancel")
    if source.count(INLINE_OLD) != 1 or source.count(INLINE_CALL_OLD) != 1:
        raise ValueError("Pinned NativeX inline capture/caller source drift")
    return (source.replace(INLINE_OLD, INLINE_NEW, 1)
                  .replace(INLINE_CALL_OLD, INLINE_CALL_NEW, 1)), True


def patch_renderer_all(source):
    source, capture_changed = patch_text(source)
    source, inline_changed = patch_inline_capture(source)
    if capture_changed != inline_changed:
        raise ValueError("Mixed Hulk DMA/inline snapshot patch on NativeX renderer")
    return source, capture_changed


def patch_draw(source):
    if RESTORE_AFTER in source:
        if RESTORE in source or source.count(RESTORE_AFTER) != 1:
            raise ValueError("Partial DMA restore ordering patch")
        return source, False
    if source.count(RESTORE) != 1 or source.count(AHEAD) != 1:
        raise ValueError("NativeX DMA restore/upload-ahead anchor drift")
    return source.replace(RESTORE, "", 1).replace(AHEAD, AHEAD + RESTORE_AFTER, 1), True

CPU_HELPER = """// HULK/CPU-INDEX-SNAPSHOT: assemble the same ordered copies that the GPU
// mirror receives. Never fill holes from newer live guest memory.
template <typename Processor, typename Result, typename Copies>
bool HulkProcessIndexCopies(Processor& processor, Result& result,
                            uint32_t base, uint32_t size, const Copies& copies) {
  if (!size) return false; // A zero-length DMA request has no safe index data.
  if (base >= 0x20000000u || size > 0x20000000u - base || size > (1u << 20))
    return false;
  // HULK/CPU-INDEX-DIRECT-SPAN: if exactly ONE captured command
  // interval intersects the index request and covers ALL of it, use the
  // already-owned bytes directly. ReXGlue ProcessCapturedIndices consumes
  // them synchronously and aligns internally. No extra scratch copy or
  // allocation; overlapping writes still use the ordered merge below.
  const uint64_t request_end = uint64_t(base) + size;
  uint32_t intersecting = 0;
  const uint8_t* direct = nullptr;
  for (const auto& copy : copies) {
    const uint64_t first = uint64_t(copy.address);
    const uint64_t last = first + copy.size;
    if (first >= request_end || uint64_t(base) >= last) continue;
    ++intersecting;
    if (!copy.data) return false;
    if (first <= base && last >= request_end)
      direct = copy.data + (uint64_t(base) - first);
  }
  // HULK/PM4-DMA-NO-LIVE-FALLBACK: an unsnapshotted DMA draw must not
  // read index bytes from the guest ring AFTER that memory was reused.
  if (!intersecting) return false;
  if (intersecting == 1 && direct)
    return processor.ProcessCapturedIndices(result, direct, base, size);
  // HULK/CPU-INDEX-INTERVAL-COVERAGE: Copy count is capped by Command
  // (two owned copies). Verify the union of their byte intervals instead
  // of allocating and filling an extra size-byte coverage mask per draw.
  // This removes one allocation and one O(index_bytes) pass for every
  // DMA-indexed Draw, while preserving last-copy-wins overlap semantics.
  // HULK/CPU-INDEX-SCRATCH-REUSE: the CPU primitive processor consumes
  // this index span synchronously before returning. Reuse its per-thread
  // scratch allocation rather than allocating it for ~1800 draws/frame
  // during destruction; bounds and last-copy-wins logic are unchanged.
  thread_local std::vector<uint8_t> bytes;
  bytes.resize(size);
  for (const auto& copy : copies) {
    const uint64_t first = std::max(uint64_t(base), uint64_t(copy.address));
    const uint64_t last = std::min(request_end, uint64_t(copy.address) + copy.size);
    if (first >= last) continue;
    if (!copy.data) return false;
    std::memcpy(bytes.data() + (first - base), copy.data + (first - copy.address), last - first);
  }
  uint64_t cursor = base;
  while (cursor < request_end) {
    uint64_t next = cursor;
    for (const auto& copy : copies) {
      const uint64_t first = uint64_t(copy.address);
      const uint64_t last = first + copy.size;
      if (first <= cursor && last > next)
        next = std::min(last, request_end);
    }
    // An uncovered byte is never read from the live guest buffer.
    if (next == cursor) return false;
    cursor = next;
  }
  return processor.ProcessCapturedIndices(result, bytes.data(), base, size);
}

"""
CPU_ANCHOR = "  if (!primitives_->Process(primitives)) {"
CPU_CALL = """  const auto hulk_cpu_draw = regs.Get<reg::VGT_DRAW_INITIATOR>();
  const uint32_t hulk_cpu_stride = hulk_cpu_draw.index_size == xenos::IndexFormat::kInt16 ? 2u : 4u;
  const bool hulk_cpu_ok = hulk_cpu_draw.source_select == xenos::SourceSelect::kDMA
      ? HulkProcessIndexCopies(*primitives_, primitives,
          regs[rg::XE_GPU_REG_VGT_DMA_BASE] & ~(hulk_cpu_stride - 1u),
          uint32_t(hulk_cpu_draw.num_indices) * hulk_cpu_stride, copies)
      : primitives_->Process(primitives);
  if (!hulk_cpu_ok) {"""

def patch_cpu_draw(source):
    if 'HULK/CPU-INDEX-SNAPSHOT' in source:
        if CPU_HELPER not in source or CPU_CALL not in source or CPU_ANCHOR in source:
            raise ValueError('Partial NativeX CPU snapshot patch')
        return source, False
    anchor = 'struct DrawContext {'
    if source.count(anchor) != 1 or source.count(CPU_ANCHOR) != 1:
        raise ValueError('NativeX CPU snapshot anchor drift')
    return ('#include <vector>\n' + source.replace(anchor, CPU_HELPER + anchor, 1)
            .replace(CPU_ANCHOR, CPU_CALL, 1)), True


def patch_draw_all(source):
    source, a = patch_draw(source)
    source, b = patch_cpu_draw(source)
    return source, a or b


# The captured data is authoritative for this draw. Comparing it with LIVE
# CPU memory says nothing about the separate GPU mirror (or earlier copies).
MEMORY_OLD = """bool GuestMemory::WriteIfChanged(IDeviceContext* context, uint32_t address, const uint8_t* data, uint32_t size) {
  if (!size || address >= kBufferSize || size > kBufferSize - address) return false;
  if (!std::memcmp(memory().TranslatePhysical(address), data, size)) return false;
  context->UpdateBuffer(buffer_, address, size, data, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  uploaded_bytes_ += size;
  ++copies_;
  ++total_copies_;
  in_read_state_ = false;
  // The mirror holds the old data there now: the next request copies the pages from guest memory again, hashes
  // or not.
  const uint32_t page_shift = page_size_log2();
  for (uint32_t page = address >> page_shift; page <= (address + size - 1) >> page_shift; ++page) {
    page_hashes_[page] = 0;
    copy_valid_[page] = false;
    checked_window_[page] = 0;
  }
  MemoryInvalidationCallback(address, size, true);
  return true;
}
"""
MEMORY_NEW = MEMORY_OLD.replace(
    "  if (!std::memcmp(memory().TranslatePhysical(address), data, size)) return false;",
    """  // HULK/CAPTURED-GPU-BYTES: CPU equality is not GPU mirror equality.
  // Earlier overlapping captured writes and queued uploads can leave the
  // mirror different even when live guest bytes match this command's bytes.
  // Preserve command order: the last captured copy wins on CPU and GPU.
  if (!data) return false;""")

def patch_memory(source):
    if MEMORY_NEW in source:
        return source, False
    if "HULK/CAPTURED-GPU-BYTES" in source or source.count(MEMORY_OLD) != 1:
        raise ValueError("NativeX captured GPU memory restore anchor drift")
    return source.replace(MEMORY_OLD, MEMORY_NEW, 1), True


MEMORY_HEADER_OLD = """  // Writes `size` bytes at `address` into the mirror if guest memory no longer holds them: an inline draw's data
  // that the game reused since the draw was captured (D71). The pages are then invalidated, so that the next draw
  // reading them takes guest memory again. Returns whether it wrote.
"""
MEMORY_HEADER_NEW = """  // Writes captured command bytes into the GPU mirror, in command order.
  // The historical name is retained; live CPU equality is not a valid reason
  // to skip the GPU write. Invalidates pages for subsequent live requests.
  // Returns whether it wrote; rejects null data and invalid/empty ranges.
"""

def patch_memory_header(source):
    if MEMORY_HEADER_NEW in source:
        return source, False
    if source.count(MEMORY_HEADER_OLD) != 1:
        raise ValueError("NativeX captured GPU memory header anchor drift")
    return source.replace(MEMORY_HEADER_OLD, MEMORY_HEADER_NEW, 1), True


def main():
    if len(sys.argv)!=2:
        raise SystemExit("Usage: patch_native_x_hulk_packet_dma_snapshot.py <NativeX checkout>")
    root=Path(sys.argv[1]).resolve()
    actual=subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if actual!=PIN:
        raise ValueError("Unexpected NativeX checkout SHA: "+actual)
    edits=[]
    for name, action in (("src/renderer.cpp", patch_renderer_all), ("src/draw.cpp", patch_draw_all),
                         ("src/guest_memory.cpp", patch_memory),
                         ("src/guest_memory.h", patch_memory_header)):
        path=root/name
        result,changed=action(path.read_text(encoding="utf-8"))
        edits.append((path,result,changed))
    for path,result,changed in edits:
        if changed:path.write_text(result,encoding="utf-8",newline="\n")
    changed=any(edit[2] for edit in edits)
    print("Hulk PM4 per-packet bounded index snapshot:", "patched" if changed else "already installed")
if __name__=="__main__":main()
