#!/usr/bin/env python3
"""Hulk Vulkan: keep scene draws while host shaders and pipelines compile.

The user's actual build-67 log counts 1297 skipped translations and 1098
skipped pipelines by frame 1200, despite 1932 draws in that frame. The
upstream D101 wait applies only to offscreen targets, not the main scene.
This opt-in HULK-only CVAR uses that SAME bounded wait, never invents shaders
or changes any guest state or rendering command order.
"""
from pathlib import Path
import os
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
                     "bf965e952c98e050753e91d08f32685a9f754b12")
SUPPORTED = {
    "bf965e952c98e050753e91d08f32685a9f754b12",
    "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e",
}
PATH = "src/renderer.cpp"
MARKER = "HULK/SCENE-PIPELINE-WAIT"
DECL_ANCHOR = "namespace native_x_renderer::renderer {\nnamespace {"
DECL_NEW = (
    'REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_wait_scene_pipelines, false, '
    '"NativeXRenderer",\n'
    '                    "HULK/SCENE-PIPELINE-WAIT: wait up to the existing D101 timeout for scene shaders and pipelines, rather than skipping drawing.");\n\n'
    + DECL_ANCHOR
)
WAIT_ANCHOR = (
    "  g_draw->SetWaitForPipelines("
    "REXCVAR_GET(native_x_renderer_wait_offscreen_pipelines) && offscreen && !steady, steady);"
)
WAIT_NEW = (
    "  // HULK/SCENE-PIPELINE-WAIT: the sky/character can be visible only\n"
    "  // briefly; skipping a shader or pipeline loses that draw for the\n"
    "  // entire frame. The existing D101 wait is bounded to 3 s and\n"
    "  // detects a real failed translation/pipeline without replacing it.\n"
    "  // Only Hulk opts in via its own config; other games are untouched.\n"
    "  const bool hulk_wait_scene = REXCVAR_GET(native_x_renderer_hulk_wait_scene_pipelines);\n"
    "  g_draw->SetWaitForPipelines(\n"
    "      hulk_wait_scene ||\n"
    "          (REXCVAR_GET(native_x_renderer_wait_offscreen_pipelines) && offscreen && !steady),\n"
    "      steady && !hulk_wait_scene);"
)

def patch_text(source: str) -> tuple[str, bool]:
    if DECL_NEW in source and WAIT_NEW in source:
        return source, False
    if "native_x_renderer_hulk_wait_scene_pipelines" in source or MARKER in source:
        raise ValueError("Partial Hulk scene pipeline wait")
    if source.count(DECL_ANCHOR) != 1 or source.count(WAIT_ANCHOR) != 1:
        raise ValueError("Pinned NativeX scene/pipeline source anchors drift")
    # Two atomic edits, fail closed on unexpected upstream or Hulk changes.
    return source.replace(DECL_ANCHOR, DECL_NEW, 1).replace(WAIT_ANCHOR, WAIT_NEW, 1), True

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_scene_pipeline_wait.py NATIVE_X_ROOT")
    root = Path(sys.argv[1]).resolve()
    actual = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if actual != PIN or PIN not in SUPPORTED:
        raise ValueError("Unreviewed NativeX revision: " + actual)
    file = root / PATH
    fixed, changed = patch_text(file.read_text(encoding="utf-8"))
    if changed:
        file.write_text(fixed, encoding="utf-8", newline="\n")
    print("Hulk bounded Vulkan scene-pipeline wait:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
