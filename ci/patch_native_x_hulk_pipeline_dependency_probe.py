#!/usr/bin/env python3
"""Hulk-only, opt-in shader/pipeline wait diagnostic inspired by King Kong's
pass-dependency approach. No runtime change unless the new CVar is enabled.
Run after the pinned NativeXRenderer's Hulk patch chain.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
CVAR_OLD = 'REXCVAR_DEFINE_BOOL(native_x_renderer_wait_offscreen_pipelines, true, "NativeXRenderer",'
CVAR_NEW = (
    '// HULK/PIPELINE-DEPENDENCY-PROBE: opt-in correctness comparison, not a\n'
    '// permanent frame-pacing policy. Waits (up to NativeX existing 3s cap)\n'
    '// for ALL game draw shaders/pipelines instead of allowing scene skips.\n'
    'REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_wait_all_pipelines, false, "NativeXRenderer",\n'
    '                    "Hulk diagnostic: wait for all shader/pipeline draws, not just offscreen passes");\n'
)
DRAW_OLD = (
    '  g_draw->SetWaitForPipelines(REXCVAR_GET(native_x_renderer_wait_offscreen_pipelines) && offscreen && !steady, steady);'
)
DRAW_NEW = (
    '  // HULK/PIPELINE-DEPENDENCY-PROBE: compare a scene without async\n'
    '  // shader/pipeline skips (King Kong distinguishes critical passes).\n'
    '  // Preserve the ordinary NativeX wait policy bit-for-bit when off.\n'
    '  const bool hulk_wait_all = REXCVAR_GET(native_x_renderer_hulk_wait_all_pipelines);\n'
    '  const bool hulk_wait = hulk_wait_all ||\n'
    '      (REXCVAR_GET(native_x_renderer_wait_offscreen_pipelines) && offscreen && !steady);\n'
    '  g_draw->SetWaitForPipelines(hulk_wait, steady && !hulk_wait_all);'
)

def patch_text(source: str) -> tuple[str, bool]:
    new_cvar = CVAR_NEW in source
    new_draw = DRAW_NEW in source
    if new_cvar and new_draw:
        return source, False
    if new_cvar or new_draw or "native_x_renderer_hulk_wait_all_pipelines" in source:
        raise ValueError("Partial Hulk pipeline-dependency probe")
    for anchor in (CVAR_OLD, DRAW_OLD):
        if source.count(anchor) != 1:
            raise ValueError("NativeX pipeline-wait anchor drift: " + anchor[:90])
    return (source.replace(CVAR_OLD, CVAR_NEW + CVAR_OLD, 1)
                  .replace(DRAW_OLD, DRAW_NEW, 1)), True

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_pipeline_dependency_probe.py <NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    actual = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"],
                                     text=True).strip()
    if actual != PIN:
        raise ValueError("Unexpected pinned NativeX revision: " + actual)
    path = root / "src/renderer.cpp"
    patched, changed = patch_text(path.read_text(encoding="utf-8"))
    if changed:
        path.write_text(patched, encoding="utf-8", newline="\n")
    print("Hulk optional pipeline-dependency comparison:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
