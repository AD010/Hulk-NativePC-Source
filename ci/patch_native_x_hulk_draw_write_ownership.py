#!/usr/bin/env python3
"""Fix Hulk NativeX image ownership for skipped GPU Draw, and masked RTs.

A renderer claiming a write that did not happen picks the wrong shared
1x/4x source at Resolve time, especially after shader/pipeline compilation
skips. Touches only native Vulkan ownership bookkeeping, never Xenos.
"""
from pathlib import Path
import subprocess
import sys

import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
PATCHES = {
 "src/draw.h": [("  void Draw(const StateTracker::Program& vertex_program, const StateTracker::Program& pixel_program,\n            std::span<const Copy> copies, const std::array<HostTarget*, 5>& targets, bool tiled);", "  // True only if DrawInternal submitted host rasterization.\n  bool Draw(const StateTracker::Program& vertex_program, const StateTracker::Program& pixel_program,\n            std::span<const Copy> copies, const std::array<HostTarget*, 5>& targets, bool tiled);")],
 "src/draw.cpp": [
     ("void DrawPath::Draw(const StateTracker::Program& vertex_program, const StateTracker::Program& pixel_program,\n                    std::span<const Copy> copies, const std::array<HostTarget*, 5>& targets, bool tiled) {", "bool DrawPath::Draw(const StateTracker::Program& vertex_program, const StateTracker::Program& pixel_program,\n                    std::span<const Copy> copies, const std::array<HostTarget*, 5>& targets, bool tiled) {"),
     ("  if (DrawInternal(vertex_program, pixel_program, copies, targets)) {\n    ++stats_.drawn;\n  }\n}", "  const bool submitted = DrawInternal(vertex_program, pixel_program, copies, targets);\n  if (submitted) ++stats_.drawn;\n  return submitted;\n}")
 ],
 "src/renderer.cpp": [
     ("  for (uint32_t i = 0; i < 5; ++i) {\n    if (targets[i]) targets[i]->written = ++g_target_writes;\n    if (targets[i] && !first_target) first_target = targets[i];\n  }", "  // Mark image ownership only AFTER a draw has actually been submitted.\n  // A missing shader or compiling pipeline has not written this frame.\n  for (uint32_t i = 0; i < 5; ++i) {\n    if (targets[i] && !first_target) first_target = targets[i];\n  }"),
     ("  g_draw->Draw(c.programs[0] ? *c.programs[0] : StateTracker::kNoProgram,\n               c.programs[1] ? *c.programs[1] : StateTracker::kNoProgram, std::span(copies.data(), c.copy_count),\n               targets, g_tiling);", "  const bool submitted = g_draw->Draw(c.programs[0] ? *c.programs[0] : StateTracker::kNoProgram,\n               c.programs[1] ? *c.programs[1] : StateTracker::kNoProgram, std::span(copies.data(), c.copy_count),\n               targets, g_tiling);\n  // Xenos color mask controls which RT is writable. RB_DEPTHCONTROL:\n  // bit0 stencil_enable, bit1 z_enable, bit2 z_write_enable.\n  // Do NOT bump unrelated bound images: NativeX's 1x/4x sample-grid\n  // resolve selects the most recently genuinely written RT.\n  if (submitted) {\n    const uint32_t mask = (*g_registers)[rex::graphics::reg::RB_COLOR_MASK::register_index];\n    const uint32_t depth = (*g_registers)[rex::graphics::reg::RB_DEPTHCONTROL::register_index];\n    for (uint32_t i = 0; i < 5; ++i) {\n      if (!targets[i]) continue;\n      const bool can_write = i < 4\n          ? ((mask >> (4 * i)) & 0xFu) != 0\n          : ((depth & 1u) != 0 || (depth & 6u) == 6u);\n      if (can_write) targets[i]->written = ++g_target_writes;\n    }\n  }")
 ]
}

def transform(source, replacements):
    has_new=[new in source for old,new in replacements]
    if all(has_new):return source,False
    if any(has_new):raise ValueError("Partial Hulk draw ownership repair")
    for old,new in replacements:
        if source.count(old)!=1:raise ValueError("NativeX source drift / duplicate: "+old[:100])
    for old,new in replacements:source=source.replace(old,new,1)
    return source,True

def main():
    if len(sys.argv)!=2:raise SystemExit("Usage: patch_native_x_hulk_draw_write_ownership.py <NativeX>")
    root=Path(sys.argv[1]).resolve()
    sha=subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if sha!=PIN:raise ValueError("NativeX SHA not pinned "+sha)
    changed_all=[]
    for rel,rules in PATCHES.items():
        p=root/rel
        after,changed=transform(p.read_text(encoding="utf8"),rules)
        changed_all.append((p,after,changed))
    if len({changed for _,_,changed in changed_all})!=1:
        raise ValueError("Partial source patch across files; no writes")
    for p,after,changed in changed_all:
        if changed:p.write_text(after,encoding="utf8",newline="\n")
    print("Hulk NativeX submitted/masked RT ownership:", "patched" if changed_all[0][2] else "already patched")
if __name__=="__main__":main()
