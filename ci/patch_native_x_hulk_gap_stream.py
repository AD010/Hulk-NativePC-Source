#!/usr/bin/env python3
"""Replay ordered Xenos PM4 draw packets in Hulk's validated unhooked gaps.

The earlier single-direct-draw bridge missed gaps with 24 draw packets and
three indirect buffers. Replaying only the final register/shader state cannot
reproduce intermediate draws. This adds an opt-in packet-stream callback,
retaining the existing native Vulkan CaptureDraw and its per-draw record.

Must run after patch_native_x_hulk_pm4_gap.py and
patch_native_x_hulk_gap_draw.py on the pinned NativeXRenderer revision.
"""
from pathlib import Path
import subprocess
import sys

import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
PATCHES = {
  "src/state.h": [
    ("#include <cstdint>\n#include <memory>",
     "#include <cstdint>\n#include <functional>\n#include <memory>"),
    ("  bool TakeHulkGapSingleDraw(uint32_t& device);",
     "  void SetHulkGapPacketDrawCallback(\n"
     "      std::function<void(StateTracker&, uint32_t, const uint8_t*)> callback) {\n"
     "    hulk_gap_packet_callback_ = std::move(callback);\n"
     "  }\n"
     "  bool TakeHulkGapSingleDraw(uint32_t& device);"),
    ("  bool hulk_gap_single_draw_pending_ = false;",
     "  // Native Vulkan callback: capture each *ordered* GPU packet draw.\n"
     "  std::function<void(StateTracker&, uint32_t, const uint8_t*)> hulk_gap_packet_callback_;\n"
     "  bool hulk_gap_single_draw_pending_ = false;"),
  ],
  "src/state.cpp": [
    ("void ScanPackets(const uint8_t* base, uint32_t begin, uint32_t end, PacketScan& out, int depth,\n"
     "                 bool skip_constants, bool copy_microcode) {",
     "void ScanPackets(const uint8_t* base, uint32_t begin, uint32_t end, PacketScan& out, int depth,\n"
     "                 bool skip_constants, bool copy_microcode,\n"
     "                 const std::function<void()>& on_packet_draw = {}) {"),
    ("          ++out.predicated;\n"
     "          ScanDraw(base, opcode, p, count, out);\n"
     "        } else if (header & 1) {",
     "          ++out.predicated;\n"
     "          ScanDraw(base, opcode, p, count, out);\n"
     "          if (on_packet_draw) on_packet_draw();\n"
     "        } else if (header & 1) {"),
    ("              ScanPackets(base, target, target + 4 * length, out, depth + 1, skip_constants, copy_microcode);",
     "              ScanPackets(base, target, target + 4 * length, out, depth + 1,\n"
     "                          skip_constants, copy_microcode, on_packet_draw);"),
    ("            case 0x22:  // DRAW_INDX\n"
     "            case 0x36:  // DRAW_INDX_2\n"
     "              ScanDraw(base, opcode, p, count, out);\n"
     "              break;",
     "            case 0x22:  // DRAW_INDX\n"
     "            case 0x36:  // DRAW_INDX_2\n"
     "              ScanDraw(base, opcode, p, count, out);\n"
     "              if (on_packet_draw) on_packet_draw();\n"
     "              break;"),
    ("        const bool single_direct_draw = scan_.draws == 1 &&\n"
     "            scan_.indirect == 0 && !scan_.shader_loads.empty() &&\n"
     "            call != Call::kResolve && call != Call::kPresent &&\n"
     "            call != Call::kBeginTiling && call != Call::kEndTiling;\n"
     "        Gather(p, call, false, base);\n"
     "        if (single_direct_draw && keep_record_) {\n"
     "          hulk_gap_single_draw_device_ = p.device;\n"
     "          hulk_gap_single_draw_pending_ = true;\n"
     "        }",
     "        // Validate every packet before emitting any draw, then replay the\n"
     "        // exact order, including first-level INDIRECT_BUFFER contents.\n"
     "        // 4096 draws / 256 indirect buffers are conservative safety caps.\n"
     "        // Never synthesize a whole multi-draw gap from its final state.\n"
     "        const bool stream_gap = keep_record_ && hulk_gap_packet_callback_ &&\n"
     "            scan_.draws > 0 && scan_.draws <= 4096 && scan_.indirect <= 256 &&\n"
     "            call != Call::kResolve && call != Call::kPresent &&\n"
     "            call != Call::kBeginTiling && call != Call::kEndTiling;\n"
     "        if (stream_gap) {\n"
     "          const uint32_t expected = scan_.draws;\n"
     "          const uint32_t indirect_buffers = scan_.indirect;\n"
     "          scan_.Clear();\n"
     "          uint32_t emitted = 0;\n"
     "          const auto on_draw = [&]() {\n"
     "            // Gather without XDK shadow overwrite: PM4 register writes,\n"
     "            // constants and microcode belong to this draw, not the last.\n"
     "            Gather(p, call, false, base);\n"
     "            hulk_gap_packet_callback_(*this, p.device, base);\n"
     "            ++emitted;\n"
     "            scan_.Clear();\n"
     "          };\n"
     "          ScanPackets(base, last_hulk_segment_end_, p.begin, scan_, 0,\n"
     "                      false, true, on_draw);\n"
     "          if (!scan_.complete || emitted != expected) {\n"
     "            static std::atomic<uint32_t> mismatches{0};\n"
     "            if (mismatches.fetch_add(1) < 8) REXLOG_ERROR(\n"
     "                \"[HULK/GAP-STREAM] replay mismatch emitted={} expected={} complete={}\",\n"
     "                emitted, expected, scan_.complete);\n"
     "          }\n"
     "          // Keep post-last-draw state for subsequent high-level calls.\n"
     "          if (scan_.complete && (!scan_.writes.empty() ||\n"
     "              !scan_.loads.empty() || !scan_.shader_loads.empty())) {\n"
     "            Gather(p, call, false, base);\n"
     "          }\n"
     "          static std::atomic<uint64_t> total{0};\n"
     "          const uint64_t before = total.fetch_add(emitted);\n"
     "          if (!before || before / 1000 != (before + emitted) / 1000) {\n"
     "            REXLOG_INFO(\"[HULK/GAP-STREAM] native Vulkan replayed {} ordered PM4 draws ({} in this gap, {} indirect buffers)\",\n"
     "                        before + emitted, emitted, indirect_buffers);\n"
     "          }\n"
     "        } else {\n"
     "          // Original conservative state bridge still covers disabled\n"
     "          // callbacks, but never fakes several GPU draws as one.\n"
     "          const bool single_direct_draw = scan_.draws == 1 &&\n"
     "              scan_.indirect == 0 && !scan_.shader_loads.empty() &&\n"
     "              call != Call::kResolve && call != Call::kPresent &&\n"
     "              call != Call::kBeginTiling && call != Call::kEndTiling;\n"
     "          Gather(p, call, false, base);\n"
     "          if (single_direct_draw && keep_record_ && !hulk_gap_packet_callback_) {\n"
     "            hulk_gap_single_draw_device_ = p.device;\n"
     "            hulk_gap_single_draw_pending_ = true;\n"
     "          }\n"
     "        }"),
  ],
  "src/native_x_renderer.cpp": [
    ("    g_tracker->set_keep_record(g_renderer);",
     "    g_tracker->set_keep_record(g_renderer);\n"
     "    if (g_renderer) {\n"
     "      g_tracker->SetHulkGapPacketDrawCallback(\n"
     "          [](StateTracker& tracker, uint32_t device, const uint8_t* base) {\n"
     "            renderer::OnHulkGapSingleDraw(tracker, device, base);\n"
     "          });\n"
     "    }"),
  ],
  "src/renderer.h": [
    ("void OnHulkGapSingleDraw(StateTracker& tracker, uint32_t device, uint8_t* base);",
     "void OnHulkGapSingleDraw(StateTracker& tracker, uint32_t device, const uint8_t* base);"),
  ],
  "src/renderer.cpp": [
    ("void OnHulkGapSingleDraw(StateTracker& tracker, uint32_t device, uint8_t* base) {",
     "void OnHulkGapSingleDraw(StateTracker& tracker, uint32_t device, const uint8_t* base) {"),
    ("  if (n == 1 || n % 120 == 0) {\n"
     "    REXLOG_INFO(\"[HULK/GAP-DRAW] NativeX Vulkan captured {} direct unhooked draws; device={:#x}\", n, device);",
     "  if (n == 1 || n % 1000 == 0) {\n"
     "    REXLOG_INFO(\"[HULK/GAP-DRAW] NativeX Vulkan captured {} ordered PM4 packet draws; device={:#x}\", n, device);"),
  ],
}

def transform(source, rules):
    complete = [new in source for old, new in rules]
    if all(complete):
        return source, False
    if any(complete):
        raise ValueError("Partial Hulk ordered PM4 stream patch")
    for old, new in rules:
        if source.count(old) != 1:
            raise ValueError("Pinned NativeX packet-stream anchor mismatch")
    for old, new in rules:
        source = source.replace(old, new)
    return source, True

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_gap_stream.py <NativeXRenderer checkout>")
    root = Path(sys.argv[1]).resolve()
    revision = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != PIN:
        raise ValueError(f"Unexpected NativeXRenderer revision {revision}")
    changes = []
    for relative, rules in PATCHES.items():
        target = root / relative
        source, modified = transform(target.read_text(encoding="utf-8"), rules)
        changes.append((target, source, modified))
    if len(set(modified for _, _, modified in changes)) != 1:
        raise ValueError("Mixed Hulk gap stream patch state; refusing to write")
    for target, source, modified in changes:
        if modified:
            target.write_text(source, encoding="utf-8", newline="\n")
    print("Hulk ordered indirect PM4 Vulkan replay:",
          "installed" if changes[0][2] else "already installed")

if __name__ == "__main__":
    main()
