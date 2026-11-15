#!/usr/bin/env python3
"""NativeX D140: replay original PM4 gap draws from immutable FIRST-pass state.

The pre-scan already owns original shader ucode, ordered XDK SET/LOAD values,
and BIN state. The old second scan reads guest ring memory again, potentially
after the game overwrites it, then submits Vulkan draws BEFORE later replay
mismatches are detected. This bounded optimization uses owned snapshots from
the FIRST scan, postponing ALL native GPU callbacks until the ENTIRE original
gap has been validated. It never guesses a header, repairs invalid packets,
or synthesizes any original draw. Conservative old path retained on budget.
"""
from __future__ import annotations
from pathlib import Path
import subprocess
import os
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
                     "fa19ea85fec5c7f7f8a2df82d6c4a86548817b52")
SUPPORTED = {"fa19ea85fec5c7f7f8a2df82d6c4a86548817b52"}
MARK = "HULK/PM4-FIRST-PASS-OWNED-DRAWS-211"
PRE_OLD = """    std::vector<std::pair<size_t, size_t>> hulk_prescan_draw_ends;
    const auto hulk_on_prescan_draw = [&]() {
      hulk_prescan_draw_ends.emplace_back(
          scan_.register_order.size(), scan_.shader_loads.size());
    };"""
PRE_NEW = """    // HULK/PM4-FIRST-PASS-OWNED-DRAWS-211: copy register/IM_LOAD snapshots
    // DURING the first validated parse, not after the guest recycles the ring.
    // Bound cumulative snapshots to 8 MiB / 128 draws per unhooked gap.
    std::vector<PacketScan> hulk_owned_draws;
    uint64_t hulk_owned_bytes = 0;
    bool hulk_owned_complete = true;
    std::vector<std::pair<size_t, size_t>> hulk_prescan_draw_ends;
    const auto hulk_on_prescan_draw = [&]() {
      hulk_prescan_draw_ends.emplace_back(
          scan_.register_order.size(), scan_.shader_loads.size());
      if (!hulk_owned_complete) return;
      const uint64_t owned_size =
          uint64_t(scan_.writes.size() + scan_.predicated_writes.size() +
                   scan_.loads.size()) * sizeof(std::pair<uint32_t, uint32_t>) +
          uint64_t(scan_.shader_loads.size()) * sizeof(PacketScan::ShaderLoad) +
          uint64_t(scan_.shader_ucode.size() + scan_.register_order.size()) * 4u;
      if (hulk_owned_draws.size() >= 128u ||
          owned_size > (8u << 20) - hulk_owned_bytes) {
        hulk_owned_complete = false;
        hulk_owned_draws.clear();
        return;
      }
      hulk_owned_bytes += owned_size;
      hulk_owned_draws.emplace_back(scan_);
    };"""
START_OLD = "          const uint32_t indirect_buffers = scan_.indirect;"
START_NEW = """          const uint32_t indirect_buffers = scan_.indirect;
          // Restore the complete first-scan geometry/shader state for each
          // ORIGINAL draw in packet order. No mutable guest PM4 re-scan, and
          // never submit ANY GPU work from an incomplete original stream.
          // Gather may use the current SDK device shadow for unchanged state;
          // only original packet state, ucode and BIN ownership are frozen.
          if (hulk_owned_complete && hulk_owned_draws.size() == expected) {
            PacketScan hulk_final_state = std::move(scan_);
            for (auto& owned : hulk_owned_draws) {
              scan_ = std::move(owned);
              Gather(p, call, false, base);
              hulk_gap_packet_callback_(*this, p.device, base);
            }
            scan_ = std::move(hulk_final_state);
            // The original stream may contain legitimate register writes
            // AFTER its last draw; preserve those exactly as recorded.
            if (!scan_.writes.empty() || !scan_.loads.empty() ||
                !scan_.shader_loads.empty())
              Gather(p, call, false, base);
            static std::atomic<uint64_t> hulk_owned_draw_total{0};
            const uint64_t n = hulk_owned_draw_total.fetch_add(expected) + expected;
            if (n == expected || n / 10000u != (n - expected) / 10000u)
              REXLOG_INFO("[HULK/PM4-FIRST-PASS-OWNED] {} authentic native "
                          "draws from one validated scan ({} indirect buffers)",
                          n, indirect_buffers);
          } else {
            // Uncommon over-budget gap: preserve the already reviewed D140
            // replay rather than changing behavior for unvalidated scenes."""
END_OLD = """        } else {
          // Original conservative state bridge still covers disabled"""
END_NEW = """          }
        } else {
          // Original conservative state bridge still covers disabled"""

def patch(src: str) -> tuple[str, bool]:
    if MARK in src:
        if (src.count(PRE_NEW) != 1 or src.count(START_NEW) != 1 or
                src.count(END_NEW) != 1):
            raise ValueError("Partial owned first-pass draw patch")
        return src, False
    for anchor in ("HULK/PM4-PER-DRAW-BOUNDARY",
                   "HULK/PM4-REGISTER-SNAPSHOT",
                   "HULK/PM4-PRESCAN-SHADER-SNAPSHOT"):
        if anchor not in src:
            raise ValueError("NativeX D140 missing prerequisite " + anchor)
    for old, new in ((PRE_OLD, PRE_NEW),
                     (START_OLD, START_NEW),
                     (END_OLD, END_NEW)):
        if src.count(old) != 1:
            raise ValueError("Unexpected final D140 PM4 stream anchor " + old[:56])
        src = src.replace(old, new, 1)
    return src, True

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_gap_owned_draws.py <NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN or sha not in SUPPORTED:
        raise ValueError("Unreviewed NativeX D140 revision " + sha)
    target = root / "src/state.cpp"
    updated, modified = patch(target.read_text(encoding="utf-8"))
    if modified:
        target.write_text(updated, encoding="utf-8", newline="\n")
    print("Hulk immutable first-scan PM4 replay:",
          "installed" if modified else "already installed")

if __name__ == "__main__":
    main()
