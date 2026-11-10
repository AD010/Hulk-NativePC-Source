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
PAYLOAD_GUARD = '''        // HULK/PM4-PACKET-BOUNDS: shader/ALU and indirect loads must have
        // enough data for all fixed header fields. In the unpatched scanner
        // a one-dword IM_LOAD_IMMEDIATE reads p+4 outside its payload and
        // count-2 underflows; this can submit invalid shader microcode.
        if ((opcode == 0x2F && count < 3) ||
            ((opcode == 0x27 || opcode == 0x2B || opcode == 0x3F) && count < 2)) {
          out.complete = false;
          return;
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
    source = source[:a] + BIN_GUARD + PAYLOAD_GUARD + source[b:]
    source = once(source, "          switch (opcode) {\n            case 0x2D:",
                  "          switch (opcode) {\n" + BIN_SWITCH + "            case 0x2D:", "bin opcode switch")
    source = once(source,
        "            default:\n              break;\n          }\n        }\n        p += 4 * count;",
        "            default:\n              break;\n          }\n        p += 4 * count;", "remove old unpredicated switch wrapper")
    # Hulk IM_LOAD_IMMEDIATE shaders can be emitted in a recycled guest
    # ring segment. Copy bytes at scan time even if this is a normal
    # DrawIndexedVertices Leave scan (copy_microcode=false). The render
    # thread must never dereference a later-reused guest command buffer.
    # Xenia's IM_LOAD_IMMEDIATE requires start==0 and requested shader words
    # within the actual Type3 payload. Clamping a larger size and then
    # interpreting trailing PM4 as a shader silently corrupts programs.
    source = once(source,
        "              if (immediate && dwords > count - 2) dwords = count - 2;",
        "              // HULK/PM4-SHADER-BOUNDS: never truncate shader microcode.\n"
        "              // A partial load may decode the next PM4 packet as Xenos instructions.\n"
        "              if (immediate && (dwords == 0 || dwords > count - 2 ||\n"
        "                  (start_size >> 16) != 0 || (type & 3u) >= 2)) {\n"
        "                out.complete = false;\n"
        "                return;\n"
        "              }", "reject malformed immediate shader program")
    source = once(source,
        "              if (copy_microcode) {\n"
        "                out.shader_loads.push_back(",
        "              if (copy_microcode || immediate) {\n"
        "                out.shader_loads.push_back(",
        "capture IM_LOAD_IMMEDIATE code before ring reuse")
    source = once(source,
        "  draws = indirect = predicated = 0;\n  complete = true;",
        "  draws = indirect = predicated = 0;\n  bin_rejected = 0;\n  complete = true;", "Clear keeps bin state")
    source = once(source,
        "void StateTracker::Reset() {\n",
        "void StateTracker::Reset() {\n"
        "  // HULK/PFP-RESET: only full GPU reset restarts the BIN selection.\n"
        "  scan_.bin_mask = 0xFFFFFFFFull;\n"
        "  scan_.bin_select = 0xFFFFFFFFull;\n", "state reset")
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
        "    ScanPackets(base, last_hulk_segment_end_, p.begin,\n"
        "                scan_, 0, false, true);\n",
        "    ScanPackets(base, last_hulk_segment_end_, p.begin,\n"
        "                scan_, 0, false, true);\n"
        "    // HULK/PFP-AUDIT: count skipped packets only in the first scan,\n"
        "    // not in the replay (which processes this gap again).\n"
        "    if (scan_.complete && scan_.bin_rejected) {\n"
        "      static std::atomic<uint64_t> total_bin_rejected{0};\n"
        "      const uint64_t total = total_bin_rejected.fetch_add(scan_.bin_rejected) + scan_.bin_rejected;\n"
        "      if (total < 24 || total % 10000 < scan_.bin_rejected)\n"
        "        REXLOG_INFO(\"[HULK/PFP-BIN] predicated PM4 packets skipped: {} ({} in this gap)\",\n"
        "                    total, scan_.bin_rejected);\n"
        "    }\n"
        "    if (!scan_.complete) {\n"
        "      scan_.bin_mask = hulk_bin_mask_at_gap;\n"
        "      scan_.bin_select = hulk_bin_select_at_gap;\n"
        "    }\n", "first scan rollback + nonduplicated predicate audit")
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
    cpp = files["src/state.cpp"]
    hdr = files["src/state.h"]
    cpp_patched = "HULK/PFP-PREDICATE" in cpp
    hdr_patched = "uint64_t bin_mask = 0xFFFFFFFFull, bin_select = 0xFFFFFFFFull;" in hdr
    if cpp_patched != hdr_patched:
        raise ValueError("Partially applied BIN predicate patch: header/source mismatch")
    if cpp_patched:
        required = (
            "hulk_bin_mask_at_gap", "HULK/PM4-PACKET-BOUNDS",
            "if (copy_microcode || immediate)", "HULK/PM4-SHADER-BOUNDS",
            "HULK/PFP-RESET", "HULK/PFP-BIN", "bin_rejected = 0;",
        )
        if any(mark not in cpp for mark in required):
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
