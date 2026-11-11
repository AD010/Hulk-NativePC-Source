#!/usr/bin/env python3
"""Freeze PM4 shader words between NativeX gap validation and draw replay.

Pre-scan validates an unhooked XDK command-buffer gap, then scans it AGAIN
to render each draw. The game may recycle that memory between scans. Build
#67's 27-word vertex shader crashes have identical 26-word prefixes and
different final words: suggestive (not conclusive) evidence of such reuse.

Reuse PRE-SCAN shader bytes only when replay packet/word metadata matches.
Never invent microcode or change actual guest registers. Fail closed on drift.
Apply after gap_stream, BIN predicate and register_order patches.
"""
import os
from pathlib import Path
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
    "bf965e952c98e050753e91d08f32685a9f754b12")
PINS = {"bf965e952c98e050753e91d08f32685a9f754b12",
        "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}

BEFORE_HEAD = """          const uint32_t indirect_buffers = scan_.indirect;
          scan_.Clear();"""
AFTER_HEAD = """          const uint32_t indirect_buffers = scan_.indirect;
          // HULK/PM4-PRESCAN-SHADER-SNAPSHOT: the first, fully validated
          // scan owns the shader bytes, NOT a second guest-memory read.
          // Packet replay still uses the real command list; if metadata
          // differs, refuse this replay rather than inventing shader data.
          const auto hulk_prescan_loads = scan_.shader_loads;
          const auto hulk_prescan_words = scan_.shader_ucode;
          size_t hulk_prescan_cursor = 0;
          bool hulk_shader_replay_ok = true;
          const auto hulk_restore_shader_words = [&]() -> bool {
            for (const auto& load : scan_.shader_loads) {
              if (hulk_prescan_cursor >= hulk_prescan_loads.size())
                return false;
              const auto& original = hulk_prescan_loads[hulk_prescan_cursor++];
              if (load.packet != original.packet ||
                  load.address != original.address ||
                  load.dwords != original.dwords ||
                  load.type != original.type ||
                  load.immediate != original.immediate ||
                  original.ucode_offset == PacketScan::kInGuestMemory ||
                  load.ucode_offset == PacketScan::kInGuestMemory ||
                  uint64_t(original.ucode_offset) + original.dwords > hulk_prescan_words.size() ||
                  uint64_t(load.ucode_offset) + load.dwords > scan_.shader_ucode.size()) {
                return false;
              }
              std::copy_n(hulk_prescan_words.data() + original.ucode_offset,
                          load.dwords, scan_.shader_ucode.data() + load.ucode_offset);
            }
            return true;
          };
          scan_.Clear();"""
BEFORE_DRAW = """          const auto on_draw = [&]() {
            // Gather without XDK shadow overwrite: PM4 register writes,
            // constants and microcode belong to this draw, not the last.
            Gather(p, call, false, base);"""
AFTER_DRAW = """          const auto on_draw = [&]() {
            // Preserve validated shader microcode even if the guest has
            // reused the command ring before this second replay scan.
            if (!hulk_shader_replay_ok) return;
            if (!hulk_restore_shader_words()) {
              hulk_shader_replay_ok = false;
              return;
            }
            // Gather without XDK shadow overwrite: PM4 register writes,
            // constants and microcode belong to this draw, not the last.
            Gather(p, call, false, base);"""
BEFORE_MISMATCH = "          if (!scan_.complete || emitted != expected) {"
AFTER_MISMATCH = "          if (!hulk_shader_replay_ok || !scan_.complete || emitted != expected) {"
BEFORE_TAIL = """          if (scan_.complete && (!scan_.writes.empty() ||
              !scan_.loads.empty() || !scan_.shader_loads.empty())) {
            Gather(p, call, false, base);
          }"""
AFTER_TAIL = """          if (hulk_shader_replay_ok && scan_.complete &&
              (!scan_.writes.empty() ||
               !scan_.loads.empty() || !scan_.shader_loads.empty())) {
            if (hulk_restore_shader_words()) {
              Gather(p, call, false, base);
            } else {
              hulk_shader_replay_ok = false;
              static std::atomic<uint32_t> rejected_tail{0};
              if (rejected_tail.fetch_add(1) < 8)
                REXLOG_ERROR("[HULK/PM4-SHADER-SNAPSHOT] rejected changed post-draw shader payload");
            }
          }"""

def patch(src: str) -> tuple[str, bool]:
    complete = [part in src for part in
        (AFTER_HEAD, AFTER_DRAW, AFTER_MISMATCH, AFTER_TAIL)]
    if all(complete):
        return src, False
    if any(complete) or "HULK/PM4-PRESCAN-SHADER-SNAPSHOT" in src:
        raise ValueError("Partial Hulk pre-scan shader snapshot patch")
    if "HULK/PFP-PREDICATE" not in src or "HULK/PM4-REGISTER-ORDER" not in src:
        raise ValueError("BIN and chronological register patches must be installed")
    for old in (BEFORE_HEAD, BEFORE_DRAW, BEFORE_MISMATCH, BEFORE_TAIL):
        if src.count(old) != 1:
            raise ValueError("NativeX gap replay source drift: " + old[:60])
    for old, new in ((BEFORE_HEAD, AFTER_HEAD),
                     (BEFORE_DRAW, AFTER_DRAW),
                     (BEFORE_MISMATCH, AFTER_MISMATCH),
                     (BEFORE_TAIL, AFTER_TAIL)):
        src = src.replace(old, new, 1)
    return src, True


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_gap_shader_snapshot.py NativeXRoot")
    root = Path(sys.argv[1]).resolve()
    rev = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"],
                                  text=True).strip()
    if PIN not in PINS or PIN != rev:
        raise ValueError("Unsupported NativeXRenderer revision: " + rev)
    file = root / "src/state.cpp"
    before = file.read_text(encoding="utf-8")
    after, changed = patch(before)
    if changed:
        file.write_text(after, encoding="utf-8", newline="\n")
    print("Hulk validated PM4 shader snapshot ownership:",
          "installed" if changed else "already installed")


if __name__ == "__main__":
    main()
