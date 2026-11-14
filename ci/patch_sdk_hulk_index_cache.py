#!/usr/bin/env python3
"""Invalidate converted index caches for every overlapping guest memory write.

The SDK compared a cached range's end with the write's END, missing equal
and fully covered ranges. This changes only half-open interval overlap;
it does not rewrite guest memory, disable caching, or invent index bytes.
"""
from pathlib import Path
import sys

FILE = "src/graphics/primitive_processor.cpp"
MARKER = "HULK/INDEX-CACHE-OVERLAP"
OLD = """          if (entry_key.base < physical_address_end) {
            uint32_t entry_end = entry_key.base + entry_key.GetSizeBytes();
            if (entry_end > physical_address_end) {"""
NEW = """          if (entry_key.base < physical_address_end) {
            uint32_t entry_end = entry_key.base + entry_key.GetSizeBytes();
            // HULK/INDEX-CACHE-OVERLAP: [entry.base, entry.end) intersects
            // [write.start, write.end) when entry.end exceeds write.START.
            // Comparing with write.end retains stale conversions when a
            // write covers the whole entry, including an exact rewrite.
            if (entry_end > physical_address_start) {"""


def patch(source):
    if MARKER in source:
        if source.count(NEW) != 1 or OLD in source:
            raise ValueError("Partial index-cache overlap fix")
        return source, False
    if source.count(OLD) != 1:
        raise ValueError("SDK index-cache invalidation anchor drift")
    return source.replace(OLD, NEW, 1), True


HEADER = "include/rex/graphics/primitive_processor.h"
DECL = "  bool Process(ProcessingResult& result_out);"
DECL_NEW = DECL + """
  // HULK/CPU-INDEX-SNAPSHOT: captured draw bytes, never guest-memory rollback.
  bool ProcessCapturedIndices(ProcessingResult& result_out, const uint8_t* data,
                              uint32_t base, uint32_t size);
 private:
  bool ProcessInternal(ProcessingResult& result_out, const uint8_t* captured_indices);
 public:
"""
PROCESS = "bool PrimitiveProcessor::Process(ProcessingResult& result_out) {"
PROCESS_NEW = """bool PrimitiveProcessor::Process(ProcessingResult& result_out) {
  return ProcessInternal(result_out, nullptr);
}

bool PrimitiveProcessor::ProcessCapturedIndices(ProcessingResult& result_out,
                                                const uint8_t* data,
                                                uint32_t base, uint32_t size) {
  // HULK/CPU-INDEX-SNAPSHOT: validate before consulting any conversion cache.
  const auto draw = register_file_.Get<reg::VGT_DRAW_INITIATOR>();
  const auto dma = register_file_.Get<reg::VGT_DMA_SIZE>();
  const uint32_t stride = draw.index_size == xenos::IndexFormat::kInt16 ? 2u : 4u;
  const uint32_t expected_base = register_file_[XE_GPU_REG_VGT_DMA_BASE] & ~(stride - 1u);
  const uint32_t expected_size = uint32_t(draw.num_indices) * stride;
  if (!data || draw.source_select != xenos::SourceSelect::kDMA ||
      !expected_size || draw.num_indices > dma.num_words || base != expected_base ||
      size != expected_size || base >= SharedMemory::kBufferSize ||
      size > SharedMemory::kBufferSize - base) return false;
  // Command storage is byte-packed. Typed CPU reads require natural alignment.
  // HULK/CPU-INDEX-ALIGNED-SCRATCH: ProcessInternal consumes the
  // aligned snapshot synchronously. Avoid an additional allocation on
  // every indexed draw while retaining exact owned command bytes.
  thread_local std::vector<uint32_t> aligned;
  aligned.resize((size + 3u) / 4u);
  std::memcpy(aligned.data(), data, size);
  // The existing cache key names live physical memory, not a captured version.
  // Invalidate this range on entry AND exit, including every early return.
  // Other ranges retain their caches; no guest bytes are rewritten.
  struct CapturedRange {
    PrimitiveProcessor& processor;
    uint32_t base, size;
    ~CapturedRange() { processor.MemoryInvalidationCallback(base, size, true); }
  } range{*this, base, size};
  MemoryInvalidationCallback(base, size, true);
  return ProcessInternal(result_out, reinterpret_cast<const uint8_t*>(aligned.data()));
}

bool PrimitiveProcessor::ProcessInternal(ProcessingResult& result_out,
                                         const uint8_t* captured_indices) {"""
READS = {
    'memory_.TranslatePhysical(guest_index_base)':
        '(captured_indices ? captured_indices : memory_.TranslatePhysical(guest_index_base))',
    'memory_.TranslatePhysical<const uint16_t*>(guest_index_base)':
        '(captured_indices ? reinterpret_cast<const uint16_t*>(captured_indices) : memory_.TranslatePhysical<const uint16_t*>(guest_index_base))',
    'memory_.TranslatePhysical<const uint32_t*>(guest_index_base)':
        '(captured_indices ? reinterpret_cast<const uint32_t*>(captured_indices) : memory_.TranslatePhysical<const uint32_t*>(guest_index_base))',
}

def patch_snapshot(source, header):
    if 'HULK/CPU-INDEX-SNAPSHOT' in source or 'HULK/CPU-INDEX-SNAPSHOT' in header:
        if PROCESS_NEW not in source or DECL_NEW not in header or any(
                source.count(v) != n for v, n in zip(READS.values(), (1, 1, 2))):
            raise ValueError('Partial CPU index snapshot patch')
        return source, header, False
    if source.count(PROCESS) != 1 or header.count(DECL) != 1:
        raise ValueError('CPU snapshot API anchor drift')
    for old, count in zip(READS, (1, 1, 2)):
        if source.count(old) != count:
            raise ValueError('CPU index consumer anchor drift: ' + old)
    source = source.replace(PROCESS, PROCESS_NEW, 1)
    for old, new in READS.items():
        source = source.replace(old, new)
    source = '#include <vector>\n' + source
    return source, header.replace(DECL, DECL_NEW, 1), True


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_sdk_hulk_index_cache.py SDK_ROOT")
    path = Path(sys.argv[1]) / FILE
    result, changed = patch(path.read_text(encoding="utf-8-sig"))
    header_path = Path(sys.argv[1]) / HEADER
    result, header, snapshot_changed = patch_snapshot(result, header_path.read_text(encoding="utf-8-sig"))
    changed = changed or snapshot_changed
    if snapshot_changed:
        header_path.write_text(header, encoding="utf-8", newline="\n")
    if changed:
        path.write_text(result, encoding="utf-8", newline="\n")
    print("Hulk converted index cache overlap:", "patched" if changed else "already patched")


if __name__ == "__main__":
    main()
