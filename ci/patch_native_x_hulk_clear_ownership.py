#!/usr/bin/env python3
"""Fix NativeX shared Vulkan content ownership for skipped color/depth clears.

Pinned upstream bf965e. No PPC, XEX, textures, or Vulkan backend substitution.
"""
import json
from pathlib import Path
import subprocess
import sys

import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
PATCHES = json.loads("[{\"old\":\"void ClearColorRect(HostTarget* target, Blitter::Rect r, const float color[4]) {\\n  auto* context = g_device->context();\\n  if (ClipToTarget(*target, r)) {\\n    context->SetRenderTargets(1, &target->view, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);\\n    context->ClearRenderTarget(target->view, color, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);\\n  } else if (r.width && r.height && FillableTarget(*target)) {\\n    g_blit.FillColor(context, target->view, HostRect(target->scale, r), color);\\n  }\\n  target->written = ++g_target_writes;\\n  if (g_draw) g_draw->TargetsChanged();\\n}\",\"new\":\"void ClearColorRect(HostTarget* target, Blitter::Rect r, const float color[4]) {\\n  auto* context = g_device->context();\\n  bool wrote = false;\\n  if (ClipToTarget(*target, r)) {\\n    context->SetRenderTargets(1, &target->view, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);\\n    context->ClearRenderTarget(target->view, color, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);\\n    wrote = true;\\n  } else if (r.width && r.height && FillableTarget(*target)) {\\n    g_blit.FillColor(context, target->view, HostRect(target->scale, r), color);\\n    wrote = true;\\n  }\\n  // A skipped/unsupported Clear has NOT changed this host image.\\n  if (wrote) {\\n    target->written = ++g_target_writes;\\n    if (g_draw) g_draw->TargetsChanged();\\n  }\\n}\"},{\"old\":\"void ClearDepthStencilRect(HostTarget* target, Blitter::Rect r, bool depth, float z, bool stencil, uint8_t s) {\\n  auto* context = g_device->context();\\n  if (ClipToTarget(*target, r)) {\\n    CLEAR_DEPTH_STENCIL_FLAGS clear_flags = CLEAR_DEPTH_FLAG_NONE;\\n    if (depth) clear_flags |= CLEAR_DEPTH_FLAG;\\n    if (stencil) clear_flags |= CLEAR_STENCIL_FLAG;\\n    context->SetRenderTargets(0, nullptr, target->view, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);\\n    context->ClearDepthStencil(target->view, clear_flags, z, s, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);\\n  } else if (r.width && r.height && FillableTarget(*target)) {\\n    g_blit.FillDepthStencil(context, target->view, HostRect(target->scale, r), depth, z, stencil, s);\\n  }\\n  target->written = ++g_target_writes;\\n  if (g_draw) g_draw->TargetsChanged();\\n}\",\"new\":\"void ClearDepthStencilRect(HostTarget* target, Blitter::Rect r, bool depth, float z, bool stencil, uint8_t s) {\\n  if (!depth && !stencil) return;\\n  auto* context = g_device->context();\\n  bool wrote = false;\\n  if (ClipToTarget(*target, r)) {\\n    CLEAR_DEPTH_STENCIL_FLAGS clear_flags = CLEAR_DEPTH_FLAG_NONE;\\n    if (depth) clear_flags |= CLEAR_DEPTH_FLAG;\\n    if (stencil) clear_flags |= CLEAR_STENCIL_FLAG;\\n    context->SetRenderTargets(0, nullptr, target->view, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);\\n    context->ClearDepthStencil(target->view, clear_flags, z, s, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);\\n    wrote = true;\\n  } else if (r.width && r.height && FillableTarget(*target)) {\\n    g_blit.FillDepthStencil(context, target->view, HostRect(target->scale, r), depth, z, stencil, s);\\n    wrote = true;\\n  }\\n  if (wrote) {\\n    target->written = ++g_target_writes;\\n    if (g_draw) g_draw->TargetsChanged();\\n  }\\n}\"}]")

def patch_text(source):
    statuses = [r["new"] in source for r in PATCHES]
    if all(statuses): return source, False
    if any(statuses): raise ValueError("Partial Hulk Clear ownership patch")
    for r in PATCHES:
        if source.count(r["old"]) != 1:
            raise ValueError("Unexpected NativeX color/depth clear anchor")
    for r in PATCHES:
        source = source.replace(r["old"], r["new"], 1)
    return source, True

def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_clear_ownership.py <NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    rev = subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"], text=True).strip()
    if rev != PIN: raise ValueError("Unexpected NativeX SHA "+rev)
    target = root / "src/renderer.cpp"
    after, changed = patch_text(target.read_text(encoding="utf8"))
    if changed: target.write_text(after,encoding="utf8",newline="\n")
    print("Hulk native Clear ownership:", "patched" if changed else "already patched")
if __name__ == "__main__": main()
