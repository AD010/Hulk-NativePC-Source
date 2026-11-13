#!/usr/bin/env python3
"""Reject out-of-bounds PM4 LOAD_ALU_CONSTANT and IM_LOAD shader memory reads.

The existing BIN patch validates packet sizes/register destination but never
checks the SOURCE guest address+word extent before dereferencing memory.
Run after bin_predication and before register_order (existing workflow only).
"""
import os
from pathlib import Path
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
    "bf965e952c98e050753e91d08f32685a9f754b12")
PINS = {"bf965e952c98e050753e91d08f32685a9f754b12",
        "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}

LOAD_OLD = "              for (uint32_t i = 0; i < size; ++i) out.loads.emplace_back(reg + i, Load32(base, address + 4 * i));"
LOAD_NEW = """              // HULK/PM4-GUEST-MEMORY-BOUNDS: physical alias is
              // 0xA0000000..0xBFFFFFFF, and register bounds alone do not
              // make a LOAD_ALU_CONSTANT guest-memory read safe.
              if (uint64_t(address) < 0xA0000000ull ||
                  uint64_t(address) + uint64_t(size) * 4ull > 0xC0000000ull) {
                out.complete = false;
                return;
              }
""" + LOAD_OLD

INLINE_OLD = "              if (immediate && dwords > count - 2) dwords = count - 2;"
INLINE_NEW = """              // HULK/PM4-SHADER-INLINE-BOUNDS: a declared shader size
              // must fit the IM_LOAD_IMMEDIATE packet. Truncating invalid
              // microcode silently can submit a corrupted shader.
              if (immediate && (count < 2 || dwords > count - 2)) {
                out.complete = false;
                return;
              }"""

SHADER_OLD = "              const auto* ucode = reinterpret_cast<const uint32_t*>(GuestPointer(base, address));"
SHADER_NEW = """              // HULK/PM4-SHADER-SOURCE-BOUNDS: no substituted shader
              // content. Reject invalid IM_LOAD or IM_LOAD_IMMEDIATE ranges.
              if (!dwords || uint64_t(address) < 0xA0000000ull ||
                  uint64_t(address) + uint64_t(dwords) * 4ull > 0xC0000000ull) {
                out.complete = false;
                return;
              }
""" + SHADER_OLD

def patch(src: str) -> tuple[str, bool]:
    ready = (LOAD_NEW in src, SHADER_NEW in src, INLINE_NEW in src)
    if all(ready):
        return src, False
    if any(ready) or "HULK/PM4-GUEST-MEMORY-BOUNDS" in src:
        raise ValueError("Partial Hulk PM4 guest source bounds patch")
    if "HULK/PFP-PREDICATE" not in src or "HULK/PM4-SHADER-BOUNDS" not in src:
        raise ValueError("Earlier BIN/SHADER bounds patch missing")
    for old in (LOAD_OLD, SHADER_OLD, INLINE_OLD):
        if src.count(old) != 1:
            raise ValueError("NativeX PM4 source read anchor drift: " + old[:48])
    return (src.replace(LOAD_OLD, LOAD_NEW, 1)
               .replace(SHADER_OLD, SHADER_NEW, 1)
               .replace(INLINE_OLD, INLINE_NEW, 1)), True

def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_guest_memory_bounds.py NativeXRoot")
    root = Path(sys.argv[1]).resolve()
    head = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if head != PIN or PIN not in PINS:
        raise ValueError("Unsupported NativeX version: " + head)
    path = root / "src/state.cpp"
    fixed, changed = patch(path.read_text(encoding="utf-8"))
    if changed:
        path.write_text(fixed, encoding="utf-8", newline="\n")
    print("Hulk PM4 ALU/shader source bounds:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
