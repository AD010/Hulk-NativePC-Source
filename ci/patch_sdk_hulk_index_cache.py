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


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_sdk_hulk_index_cache.py SDK_ROOT")
    path = Path(sys.argv[1]) / FILE
    result, changed = patch(path.read_text(encoding="utf-8-sig"))
    if changed:
        path.write_text(result, encoding="utf-8", newline="\n")
    print("Hulk converted index cache overlap:", "patched" if changed else "already patched")


if __name__ == "__main__":
    main()
