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
LEGACY_PREFLIGHT = {
    "bf965e952c98e050753e91d08f32685a9f754b12",
    "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e",
}
MARK = "HULK/PM4-FIRST-PASS-OWNED-DRAWS-211"
PRE_OLD = """    std::vector<std::pair<size_t, size_t>> hulk_prescan_draw_ends;
    const auto hulk_on_prescan_draw = [&]() {
      hulk_prescan_draw_ends.emplace_back(
          scan_.register_order.size(), scan_.shader_loads.size());
    };"""
PRE_NEW = """    // HULK/PM4-FIRST-PASS-OWNED-DRAWS-211: store ONE disjoint delta per
    // authentic first-pass draw, not the whole cumulative scan every time.
    // Repeated prefixes were quadratic in memory and reapplied old IM_LOADs,
    // fetch descriptors and constants after each earlier Vulkan draw.
    struct HulkOwnedCut {
      size_t writes = 0, loads = 0, order = 0, shaders = 0, words = 0;
      size_t predicted = 0;
      uint32_t draws = 0, indirect = 0, predicated = 0;
    };
    HulkOwnedCut hulk_cut;
    std::vector<PacketScan> hulk_owned_draws;
    uint64_t hulk_owned_bytes = 0;
    bool hulk_owned_complete = true;
    const auto hulk_delta_from = [&](const PacketScan& cumulative,
                                    PacketScan& output) -> bool {
      if (cumulative.writes.size() < hulk_cut.writes ||
          cumulative.loads.size() < hulk_cut.loads ||
          cumulative.register_order.size() < hulk_cut.order ||
          cumulative.shader_loads.size() < hulk_cut.shaders ||
          cumulative.shader_ucode.size() < hulk_cut.words ||
          cumulative.predicated_writes.size() < hulk_cut.predicted ||
          cumulative.draws < hulk_cut.draws ||
          cumulative.indirect < hulk_cut.indirect ||
          cumulative.predicated < hulk_cut.predicated) return false;
      output.Clear();
      output.writes.assign(cumulative.writes.begin() + hulk_cut.writes,
                           cumulative.writes.end());
      output.loads.assign(cumulative.loads.begin() + hulk_cut.loads,
                          cumulative.loads.end());
      output.predicated_writes.assign(
          cumulative.predicated_writes.begin() + hulk_cut.predicted,
          cumulative.predicated_writes.end());
      output.shader_ucode.assign(
          cumulative.shader_ucode.begin() + hulk_cut.words,
          cumulative.shader_ucode.end());
      for (size_t i = hulk_cut.order; i < cumulative.register_order.size(); ++i) {
        const uint32_t tag = cumulative.register_order[i];
        const bool loaded = (tag & 1u) != 0;
        const size_t absolute = tag >> 1;
        const size_t first = loaded ? hulk_cut.loads : hulk_cut.writes;
        const size_t last = loaded ? cumulative.loads.size() : cumulative.writes.size();
        if (absolute < first || absolute >= last ||
            absolute - first >= 0x7fffffffu) return false;
        output.register_order.push_back(
            (uint32_t(absolute - first) << 1) | uint32_t(loaded));
      }
      for (size_t i = hulk_cut.shaders; i < cumulative.shader_loads.size(); ++i) {
        auto shader = cumulative.shader_loads[i];
        // This first scan MUST own external IM_LOAD bytes, never a live
        // pointer into the guest command ring or an ambiguous stale index.
        if (shader.ucode_offset == PacketScan::kInGuestMemory ||
            shader.ucode_offset < hulk_cut.words ||
            uint64_t(shader.ucode_offset) + shader.dwords >
                cumulative.shader_ucode.size()) return false;
        shader.ucode_offset -= uint32_t(hulk_cut.words);
        output.shader_loads.push_back(shader);
      }
      output.draws = cumulative.draws - hulk_cut.draws;
      output.indirect = cumulative.indirect - hulk_cut.indirect;
      output.predicated = cumulative.predicated - hulk_cut.predicated;
      output.bin_mask = cumulative.bin_mask;
      output.bin_select = cumulative.bin_select;
      output.complete = cumulative.complete;
      hulk_cut = {cumulative.writes.size(), cumulative.loads.size(),
                  cumulative.register_order.size(),
                  cumulative.shader_loads.size(), cumulative.shader_ucode.size(),
                  cumulative.predicated_writes.size(), cumulative.draws,
                  cumulative.indirect, cumulative.predicated};
      return true;
    };
    std::vector<std::pair<size_t, size_t>> hulk_prescan_draw_ends;
    const auto hulk_on_prescan_draw = [&]() {
      hulk_prescan_draw_ends.emplace_back(
          scan_.register_order.size(), scan_.shader_loads.size());
      if (!hulk_owned_complete) return;
      PacketScan draw;
      if (!hulk_delta_from(scan_, draw) || draw.draws != 1u) {
        hulk_owned_complete = false;
        hulk_owned_draws.clear();
        return;
      }
      const uint64_t owned_size =
          uint64_t(draw.writes.size() + draw.loads.size() +
                   draw.predicated_writes.size()) *
              sizeof(std::pair<uint32_t, uint32_t>) +
          uint64_t(draw.shader_loads.size()) * sizeof(PacketScan::ShaderLoad) +
          uint64_t(draw.shader_ucode.size() + draw.register_order.size()) * 4u;
      if (hulk_owned_draws.size() >= 128u ||
          owned_size > (8u << 20) - hulk_owned_bytes) {
        hulk_owned_complete = false;
        hulk_owned_draws.clear();
        return;
      }
      hulk_owned_bytes += owned_size;
      hulk_owned_draws.emplace_back(std::move(draw));
    };"""
START_OLD = "          const uint32_t indirect_buffers = scan_.indirect;"
START_NEW = """          const uint32_t indirect_buffers = scan_.indirect;
          // Build the final post-last-draw packet delta FIRST. If anything
          // fails we must not dispatch even the first GPU Draw. The original
          // guest command list and every shader byte were captured once.
          PacketScan hulk_owned_tail;
          const bool hulk_owned_ready =
              hulk_owned_complete && hulk_owned_draws.size() == expected &&
              hulk_delta_from(scan_, hulk_owned_tail) &&
              hulk_owned_tail.draws == 0u;
          if (hulk_owned_ready) {
            PacketScan hulk_final_state = std::move(scan_);
            for (auto& owned : hulk_owned_draws) {
              scan_ = std::move(owned);
              Gather(p, call, false, base);
              hulk_gap_packet_callback_(*this, p.device, base);
            }
            // Only genuinely POST-LAST-DRAW register/IM_LOAD updates belong
            // to the next XDK call. Replaying the entire 24-draw prefix
            // again here previously reissued stale texture and shader state.
            scan_ = std::move(hulk_owned_tail);
            if (!scan_.register_order.empty() || !scan_.shader_loads.empty())
              Gather(p, call, false, base);
            scan_ = std::move(hulk_final_state);
            static std::atomic<uint64_t> hulk_owned_draw_total{0};
            const uint64_t n = hulk_owned_draw_total.fetch_add(expected) + expected;
            if (n == expected || n / 10000u != (n - expected) / 10000u)
              REXLOG_INFO("[HULK/PM4-FIRST-PASS-OWNED] {} native Draws "
                          "from disjoint first-scan state ({} indirect buffers)",
                          n, indirect_buffers);
          } else {
            // Over-budget gaps retain reviewed D140 conservative replay;
            // no partially reconstructed packet delta reaches the GPU."""
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
    if sha != PIN:
        raise ValueError("NativeX checkout does not match pinned revision " + sha)
    # The one preflight also exercises the historical reviewed D127/D135
    # chain. This is a D140-only production improvement: don't retroactively
    # modify working legacy source or make its regression gate fail.
    if sha in LEGACY_PREFLIGHT:
        print("Hulk immutable first-scan PM4 replay: legacy preflight unchanged")
        return
    if sha not in SUPPORTED:
        raise ValueError("Unreviewed NativeX D140 revision " + sha)
    target = root / "src/state.cpp"
    updated, modified = patch(target.read_text(encoding="utf-8"))
    if modified:
        target.write_text(updated, encoding="utf-8", newline="\n")
    print("Hulk immutable first-scan PM4 replay:",
          "installed" if modified else "already installed")

if __name__ == "__main__":
    main()
