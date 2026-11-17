#!/usr/bin/env python3
"""Exact Hulk SDK backport of Xenia Edge 4ad875 converted-index pool fix.

Pinned ReXGlue recomp branch still MULTIPLIES the SIMD alignment by the
maximum primitive conversion size. The upstream fix ADDS the alignment,
shrinking needless worst-case index page reservations by ~16x.
No changes to guest bytes, primitive topology, or index conversion.
"""
from pathlib import Path
import sys

FILE = "include/rex/graphics/primitive_processor.h"
MARKER = "HULK/XENIA-EDGE-INDEX-PAGE-4AD875"
OLD = "      sizeof(uint32_t) * (UINT16_MAX - 2) * 3 * +XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE;"
NEW = """      // HULK/XENIA-EDGE-INDEX-PAGE-4AD875: SIMD is padding, not a factor.
      sizeof(uint32_t) * (UINT16_MAX - 2) * 3 + XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE;"""


def patch(source: str) -> tuple[str, bool]:
    if MARKER in source:
        if source.count(NEW) != 1 or OLD in source:
            raise ValueError("Partial converted-index page fix")
        return source, False
    if source.count(OLD) != 1 or "kMinRequiredConvertedIndexBufferSize" not in source:
        raise ValueError("Pinned ReXGlue primitive pool expression changed")
    return source.replace(OLD, NEW, 1), True


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_sdk_hulk_index_pool.py SDK_ROOT")
    path = Path(sys.argv[1]) / FILE
    fixed, changed = patch(path.read_text(encoding="utf-8-sig"))
    if changed:
        path.write_text(fixed, encoding="utf-8", newline="\n")
    print("Hulk/Xenia Edge converted index pool: " +
          ("fixed (SIMD alignment added)" if changed else "already fixed"))


if __name__ == "__main__":
    main()
