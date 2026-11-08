#!/usr/bin/env python3
"""Hulk NativeX: replay a single direct PM4 draw found between verified XDK hooks.

Hulk logs 099/101 show unhooked GPU draws during the black startup phase.
Existing gap recovery rebuilds the post-gap state but never submits a draw.
This deliberately conservative bridge only captures one *direct*, complete
draw with a shader load and no indirect buffers. Multi-draw indirect gaps
are left untouched because last-state snapshots cannot represent them.
No Xenos host rendering or EDRAM emulation is re-enabled.
"""
from pathlib import Path
import subprocess
import sys

PIN = "bf965e952c98e050753e91d08f32685a9f754b12"

PATCHES = {
  "src/state.h": [
    (
      "  const TrackedCall* TakeCommittedInlineDraw(const uint8_t* base);",
      "  // A single direct PM4 draw from an unhooked Hulk XDK call, if safe.\n"
      "  bool TakeHulkGapSingleDraw(uint32_t& device);\n"
      "  const TrackedCall* TakeCommittedInlineDraw(const uint8_t* base);"
    ),
    (
      "  bool last_hulk_segment_valid_ = false;\n  bool skip_empty_calls_ = false;",
      "  bool last_hulk_segment_valid_ = false;\n"
      "  bool hulk_gap_single_draw_pending_ = false;\n"
      "  uint32_t hulk_gap_single_draw_device_ = 0;\n"
      "  bool skip_empty_calls_ = false;"
    ),
  ],
  "src/state.cpp": [
    (
      "  last_hulk_segment_valid_ = false;\n}",
      "  last_hulk_segment_valid_ = false;\n"
      "  hulk_gap_single_draw_pending_ = false;\n"
      "  hulk_gap_single_draw_device_ = 0;\n}"
    ),
    (
      "  p.active = true;\n  p.draw = draw;\n  p.device = device;",
      "  p.active = true;\n  p.draw = draw;\n  p.device = device;\n"
      "  hulk_gap_single_draw_pending_ = false;"
    ),
    (
      "        Gather(p, call, false, base);\n      }",
      "        // A complete one-draw gap is a real GPU draw missing from\n"
      "        // Hulk's eight high-level XDK hooks. Reconstruct its final\n"
      "        // register and shader state before capturing it. Never treat\n"
      "        // tiled resolves or multi-draw/indirect gaps as one draw.\n"
      "        const bool single_direct_draw = scan_.draws == 1 &&\n"
      "            scan_.indirect == 0 && !scan_.shader_loads.empty() &&\n"
      "            call != Call::kResolve && call != Call::kPresent &&\n"
      "            call != Call::kBeginTiling && call != Call::kEndTiling;\n"
      "        Gather(p, call, false, base);\n"
      "        if (single_direct_draw && keep_record_) {\n"
      "          hulk_gap_single_draw_device_ = p.device;\n"
      "          hulk_gap_single_draw_pending_ = true;\n"
      "        }\n      }"
    ),
    (
      "const TrackedCall* StateTracker::TakeCommittedInlineDraw(const uint8_t* base) {",
      "bool StateTracker::TakeHulkGapSingleDraw(uint32_t& device) {\n"
      "  if (!hulk_gap_single_draw_pending_) return false;\n"
      "  hulk_gap_single_draw_pending_ = false;\n"
      "  device = hulk_gap_single_draw_device_;\n"
      "  return true;\n"
      "}\n\n"
      "const TrackedCall* StateTracker::TakeCommittedInlineDraw(const uint8_t* base) {"
    ),
  ],
  "src/native_x_renderer.cpp": [
    (
      "  if (g_tracker) g_tracker->Enter(call, ctx.r3.u32, base, nested);\n  if (g_renderer) renderer::OnEnter(call, ctx, base, nested);",
      "  if (g_tracker) g_tracker->Enter(call, ctx.r3.u32, base, nested);\n"
      "  // Capture only complete single-draw gaps before the next XDK call\n"
      "  // alters its shadow state. This uses NativeX's Vulkan draw queue.\n"
      "  if (!nested && g_renderer && g_tracker) {\n"
      "    uint32_t gap_device = 0;\n"
      "    if (g_tracker->TakeHulkGapSingleDraw(gap_device)) {\n"
      "      renderer::OnHulkGapSingleDraw(*g_tracker, gap_device, base);\n"
      "    }\n"
      "  }\n"
      "  if (g_renderer) renderer::OnEnter(call, ctx, base, nested);"
    ),
  ],
  "src/renderer.h": [
    (
      "void OnLeave(const TrackedCall& tracked, StateTracker& tracker, PPCContext& ctx, uint8_t* base);",
      "void OnLeave(const TrackedCall& tracked, StateTracker& tracker, PPCContext& ctx, uint8_t* base);\n"
      "void OnHulkGapSingleDraw(StateTracker& tracker, uint32_t device, uint8_t* base);"
    ),
  ],
  "src/renderer.cpp": [
    (
      "void OnEnter(Call call, PPCContext& ctx, uint8_t* base, bool nested) {",
      "// Native Vulkan reproduction of an authentic one-draw unhooked PM4\n"
      "// sequence. The tracker has already applied the packet's registers\n"
      "// and IM_LOAD shader microcode; use its ordinary NativeX draw path.\n"
      "REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_gap_single_draw, true, \"NativeXRenderer\",\n"
      "                    \"Capture complete single direct PM4 draws in Hulk XDK gaps\");\n"
      "void OnHulkGapSingleDraw(StateTracker& tracker, uint32_t device, uint8_t* base) {\n"
      "  if (!REXCVAR_GET(native_x_renderer_hulk_gap_single_draw)) return;\n"
      "  TrackedCall synthetic{};\n"
      "  synthetic.call = Call::kDrawIndexedVertices;\n"
      "  synthetic.draw = true;\n"
      "  synthetic.device = device;\n"
      "  CaptureDraw(synthetic, tracker, base);\n"
      "  static std::atomic<uint64_t> captured{0};\n"
      "  const auto n = ++captured;\n"
      "  if (n == 1 || n % 120 == 0) {\n"
      "    REXLOG_INFO(\"[HULK/GAP-DRAW] NativeX Vulkan captured {} direct unhooked draws; device={:#x}\", n, device);\n"
      "  }\n"
      "}\n\n"
      "void OnEnter(Call call, PPCContext& ctx, uint8_t* base, bool nested) {"
    ),
  ],
}

def transform(text: str, rules: list[tuple[str,str]]) -> tuple[str,bool]:
    states = [new in text for old, new in rules]
    if all(states):
        return text, False
    if any(states):
        raise ValueError("Partially patched NativeX gap-draw source")
    for old, new in rules:
        if text.count(old) != 1:
            raise ValueError("Pinned upstream/gap patch anchor mismatch")
    for old, new in rules:
        text = text.replace(old, new)
    return text, True

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_gap_draw.py <NativeXRenderer checkout>")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if sha != PIN:
        raise ValueError("Unexpected NativeXRenderer revision: " + sha)
    outputs = []
    for relpath, rules in PATCHES.items():
        path = root / relpath
        changed_text, changed = transform(path.read_text(encoding="utf-8"), rules)
        outputs.append((path, changed_text, changed))
    if len({changed for _,_,changed in outputs}) != 1:
        raise ValueError("Mixed Hulk gap-draw patch state (refuse partial update)")
    for path, source, changed in outputs:
        if changed:
            path.write_text(source, encoding="utf-8", newline="\n")
    print("Hulk NativeX direct PM4 gap draw:", "installed" if outputs[0][2] else "already installed")

if __name__ == "__main__":
    main()
