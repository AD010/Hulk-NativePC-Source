#!/usr/bin/env python3
"""Backport Xenia Edge 344fca7 to pinned ReXGlue Xenos mip addressing.

The current 'recomp' SDK did not inherit the October 2026 Xenia fixes:
- A fully packed mip0 tail lives at the original base page even if the
  original fetch constant names a separate nonzero mip page.
- A 2x1 packed tail is laid out like a square, not a generic wide texture.
This edits only verified original texture_util source; no material guesses,
no replacement of private Textures.ARC and no shader modifications.
"""
from pathlib import Path
import sys

FILE = "src/graphics/pipeline/texture/util.cpp"
MARKER = "HULK/XENIA-EDGE-PACKED-MIPS-344FCA7"
ADDR_OLD = """  uint32_t base_page = fetch.base_address & 0x1FFFF;
  uint32_t mip_page = fetch.mip_address & 0x1FFFF;

  uint32_t mip_min_level, mip_max_level;"""
ADDR_NEW = """  uint32_t base_page = fetch.base_address & 0x1FFFF;
  uint32_t mip_page = fetch.mip_address & 0x1FFFF;
  // HULK/XENIA-EDGE-PACKED-MIPS-344FCA7: when mip 0 itself is packed,
  // ALL levels belong to the base image page. mip_address may be nonzero.
  if (base_page != 0 && fetch.packed_mips &&
      fetch.dimension != xenos::DataDimension::k1D &&
      GetPackedMipLevel(width_minus_1 + 1, height_minus_1 + 1) == 0) {
    mip_page = base_page;
  }

  uint32_t mip_min_level, mip_max_level;"""
OFFSET_START = "bool GetPackedMipOffset("
OFFSET_END = "  const FormatInfo* format_info = FormatInfo::Get(format);"
SHAPE_OLD = "    if (log2_width > log2_height) {"
SHAPE_NEW = "    if (is_wide) {"
SHAPE_INSERT_OLD = """  uint32_t packed_mip = mip - packed_mip_base;

  // Find the block offset of the mip."""
SHAPE_INSERT_NEW = """  uint32_t packed_mip = mip - packed_mip_base;

  // 2x1 follows square Xenos packed layout (not the wide-rectangle one).
  // The square/2x1 boundary is a hardware-observed source rule.
  const bool is_wide = log2_width > std::max(log2_height, uint32_t(1));

  // Find the block offset of the mip."""


def patch(source: str) -> tuple[str, bool]:
    if MARKER in source:
        if (source.count(ADDR_NEW) != 1 or SHAPE_INSERT_NEW not in source or
                source.count(SHAPE_NEW) < 2 or ADDR_OLD in source):
            raise ValueError("Partial source-proven Xenos packed-mip fix")
        return source, False
    if source.count(ADDR_OLD) != 1 or source.count(OFFSET_START) != 1 or (
            source.count(OFFSET_END) != 1):
        raise ValueError("Unknown pinned ReXGlue texture-fetch implementation")
    before, _, remaining = source.partition(OFFSET_START)
    block, _, after = remaining.partition(OFFSET_END)
    if (not block or block.count(SHAPE_INSERT_OLD) != 1 or
            block.count(SHAPE_OLD) != 2 or
            "GetPackedMipOffset" in block):
        raise ValueError("Unknown Xenos packed tail shape calculations")
    new_block = block.replace(SHAPE_INSERT_OLD, SHAPE_INSERT_NEW, 1)
    new_block = new_block.replace(SHAPE_OLD, SHAPE_NEW)
    output = (before + OFFSET_START + new_block + OFFSET_END + after).replace(
        ADDR_OLD, ADDR_NEW, 1)
    return output, True


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_sdk_hulk_packed_mip.py SDK_ROOT")
    path = Path(sys.argv[1]) / FILE
    output, changed = patch(path.read_text(encoding="utf-8-sig"))
    if changed:
        path.write_text(output, encoding="utf-8", newline="\n")
    print("Hulk/Xenia Edge authentic packed mip: " +
          ("fixed" if changed else "already installed"))


if __name__ == "__main__":
    main()
