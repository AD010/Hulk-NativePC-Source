#!/usr/bin/env python3
"""Restore Xenos PFP BIN_MASK/SELECT and predicated PM4 state in NativeX.

Apply AFTER the existing Hulk gap-stream and DRAW payload scripts. This code
is source-only; it does not embed a game or shader. It does not synthesize
Draws and never changes selected nonpredicated Draw semantics.
"""
from __future__ import annotations
import os
from pathlib import Path
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")

BIN_SWITCH = '''            // HULK/PFP-BIN: Xenos Type3 bin mask and selection, see Xenia
            // CommandProcessor::ExecutePacketType3. These persist across
            // command buffer segments; the game can change either half.
            case 0x50:  // SET_BIN_MASK (high, low)
            case 0x51: {  // SET_BIN_SELECT (high, low)
              if (count != 2) return void(out.complete = false);
              const uint64_t value = (uint64_t(Load32(base, p)) << 32) | Load32(base, p + 4);
              if (opcode == 0x50) out.bin_mask = value;
              else out.bin_select = value;
              break;
            }
            case 0x60:  // SET_BIN_MASK_LO
            case 0x61:  // SET_BIN_MASK_HI
            case 0x62:  // SET_BIN_SELECT_LO
            case 0x63: {  // SET_BIN_SELECT_HI
              if (count != 1) return void(out.complete = false);
              const uint64_t value = Load32(base, p);
              uint64_t& bits = (opcode == 0x60 || opcode == 0x61) ? out.bin_mask : out.bin_select;
              if (opcode == 0x60 || opcode == 0x62) bits = (bits & 0xFFFFFFFF00000000ull) | value;
              else bits = (bits & 0xFFFFFFFFull) | (value << 32);
              break;
            }
'''
BIN_GUARD = '''        // HULK/PFP-PREDICATE: the PFP executes Type3 packets with predicate
        // bit set only when at least one selected bin is enabled. XDK uses
        // predicated packets for tiled draws AND register/shader loads.
        // Keep the state in PacketScan across Clear() and indirect buffers.
        if (header & 1) {
          ++out.predicated;
          if ((out.bin_mask & out.bin_select) == 0) {
            ++out.bin_rejected;
            p += 4 * count;
            break;
          }
        }
'''

def once(source: str, old: str, new: str, label: str) -> str:
    if source.count(old) != 1:
        raise ValueError(f"Hulk BIN predicate source drift ({label}): anchor count={source.count(old)}")
    return source.replace(old, new, 1)

def patch_header(source: str) -> str:
    source = once(source,
        "  uint32_t draws = 0, indirect = 0, predicated = 0;\n  bool complete = true;",
        "  // Persistent PFP bin state: Xenia defaults to the low 32 bins selected. Do\n"
        "  // not reset these in Clear(), which is called after each replayed Draw.\n"
        "  uint64_t bin_mask = 0xFFFFFFFFull, bin_select = 0xFFFFFFFFull;\n"
        "  uint32_t bin_rejected = 0;\n"
        "  uint32_t draws = 0, indirect = 0, predicated = 0;\n  bool complete = true;", "header fields")
    source = once(source,
        "  // (register, value), in order: type-0/1 writes and SET_CONSTANT*, from the ring and from the indirect\n"
        "  // buffers it calls. Bin-predicated packets (per tile) are not included.",
        "  // (register, value), in original PM4 order. Executed BIN-predicated\n"
        "  // constants are included; unselected packets are ignored.", "header comment")
    return source

def patch_state(source: str) -> str:
    begin = "        if ((header & 1) && (opcode == 0x22 || opcode == 0x36)) {"
    end = "          switch (opcode) {\n"
    if source.count(begin) != 1:
        raise ValueError("Hulk BIN predicate preamble changed")
    a = source.index(begin)
    b = source.find(end, a)
    if b < 0 or b - a > 2300:
        raise ValueError("Hulk BIN predicate switch not found near preamble")
    source = source[:a] + BIN_GUARD + source[b:]
    source = once(source, "          switch (opcode) {\n            case 0x2D:",
                  "          switch (opcode) {\n" + BIN_SWITCH + "            case 0x2D:", "bin opcode switch")
    source = once(source,
        "            default:\n              break;\n          }\n        }\n        p += 4 * count;",
        "            default:\n              break;\n          }\n        p += 4 * count;", "remove old unpredicated switch wrapper")
    source = once(source,
        "  draws = indirect = predicated = 0;\n  complete = true;",
        "  draws = indirect = predicated = 0;\n  bin_rejected = 0;\n  complete = true;", "Clear keeps bin state")
    source = once(source,
        "  inline_begin_ = {};\n}",
        "  inline_begin_ = {};\n  scan_.bin_mask = 0xFFFFFFFFull;\n  scan_.bin_select = 0xFFFFFFFFull;\n}", "state reset")
    # The replay reads the same gap twice: one scan to count and validate,
    # then an ordered scan to emit the commands. Restore the BIN state at
    # gap entrance so the second pass does not predicate from future state.
    gap_prescan = ("    scan_.Clear();\n"
                   "    ScanPackets(base, last_hulk_segment_end_, p.begin,\n"
                   "                scan_, 0, false, true);")
    gap_with_snap = ("    scan_.Clear();\n"
                     "    const uint64_t hulk_bin_mask_at_gap = scan_.bin_mask;\n"
                     "    const uint64_t hulk_bin_select_at_gap = scan_.bin_select;\n"
                     "    ScanPackets(base, last_hulk_segment_end_, p.begin,\n"
                     "                scan_, 0, false, true);")
    source = once(source, gap_prescan, gap_with_snap, "gap prescan PFP checkpoint")
    source = once(source,
        "          scan_.Clear();\n          uint32_t emitted = 0;",
        "          scan_.Clear();\n"
        "          scan_.bin_mask = hulk_bin_mask_at_gap;\n"
        "          scan_.bin_select = hulk_bin_select_at_gap;\n"
        "          uint32_t emitted = 0;", "gap replay restores checkpoint")
    return source

def transform(files: dict[str,str]) -> dict[str,str]:
    if set(files) != {"src/state.cpp", "src/state.h"}:
        raise ValueError("Expected StateTracker source and header")
    if "HULK/PFP-PREDICATE" in files["src/state.cpp"]:
        if "hulk_bin_mask_at_gap" not in files["src/state.cpp"] or "bin_rejected = 0;" not in files["src/state.cpp"]:
            raise ValueError("Partially applied bin predicate patch")
        return files
    h = patch_header(files["src/state.h"])
    c = patch_state(files["src/state.cpp"])
    return {"src/state.cpp": c, "src/state.h": h}

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_bin_predication.py <fully patched NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN: raise ValueError(f"Wrong NativeX base revision: {sha}")
    paths = ["src/state.cpp", "src/state.h"]
    before = {path:(root/path).read_text(encoding="utf-8") for path in paths}
    after = transform(before)
    for path in paths:
        if after[path] != before[path]:
            (root/path).write_text(after[path], encoding="utf-8", newline="\n")
    print("Hulk native PFP BIN mask / predicated packet state:",
          "patched" if after != before else "already patched")

if __name__ == "__main__":
    main()
