#!/usr/bin/env python3
"""Bound Hulk scene shader/pipeline wait to ONE shared frame budget.

Build #67 missed world draws while async host translations/pipelines compiled.
The previous Hulk scene-wait option reused upstream D101's 3000 ms limit
PER DRAW. This could freeze the game repeatedly. Cap the Hulk-only extension
at 200 ms for ALL draws in one presented frame, and preserve unchanged D101
semantics when the Hulk option is disabled. No game/PPC bytes.
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
MARKER = "HULK/SCENE-WAIT-BUDGET"
INC_OLD = "#include <atomic>\n#include <cstdint>"
INC_NEW = "#include <atomic>\n#include <chrono>\n#include <cstdint>"
HEADER_OLD = """  void SetWaitForPipelines(bool on, bool steady_pass) {
    wait_for_pipelines_ = on;
    steady_pass_ = steady_pass;
  }"""
HEADER_NEW = """  void SetWaitForPipelines(bool on, bool steady_pass, bool hulk_scene = false) {
    wait_for_pipelines_ = on;
    steady_pass_ = steady_pass;
    hulk_scene_wait_ = hulk_scene;
  }"""
FIELDS_OLD = """  bool wait_for_pipelines_ = false;  // D101
  bool steady_pass_ = false;         // D126"""
FIELDS_NEW = """  bool wait_for_pipelines_ = false;  // D101
  bool steady_pass_ = false;         // D126
  bool hulk_scene_wait_ = false;
  uint64_t hulk_scene_wait_frame_ = ~uint64_t(0);
  std::chrono::steady_clock::time_point hulk_scene_wait_start_{};"""
DRAW_OLD = """  const auto wait_start = std::chrono::steady_clock::now();
  bool waited = false;"""
DRAW_NEW = """  const auto wait_start = std::chrono::steady_clock::now();
  // HULK/SCENE-WAIT-BUDGET: never wait up to three seconds PER WORLD DRAW.
  // All scene draws in the current presentation frame share 200 ms total.
  // Preserve upstream's full D101 wait for non-Hulk/offscreen callers.
  if (hulk_scene_wait_ && hulk_scene_wait_frame_ != device_->frames_presented()) {
    hulk_scene_wait_frame_ = device_->frames_presented();
    hulk_scene_wait_start_ = wait_start;
  }
  const auto hulk_wait_deadline = hulk_scene_wait_
      ? std::min(wait_start + kPipelineWaitLimit,
                 hulk_scene_wait_start_ + std::chrono::milliseconds(200))
      : wait_start + kPipelineWaitLimit;
  bool waited = false;"""
LOOP_OLD = "std::chrono::steady_clock::now() - wait_start < kPipelineWaitLimit"
LOOP_NEW = "std::chrono::steady_clock::now() < hulk_wait_deadline"
RENDER_OLD = "      steady && !hulk_wait_scene);"
RENDER_NEW = "      steady && !hulk_wait_scene, hulk_wait_scene);"

def once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise ValueError(f"{label}: expected one anchor, got {count}")
    return source.replace(old, new, 1)

def patch(files: dict[str, str]) -> tuple[dict[str, str], bool]:
    if set(files) != {"src/renderer.cpp", "src/draw.h", "src/draw.cpp"}:
        raise ValueError("Require renderer, draw header and draw implementation")
    renderer, hdr, cpp = (files[key] for key in
        ("src/renderer.cpp", "src/draw.h", "src/draw.cpp"))
    complete = (RENDER_NEW in renderer, HEADER_NEW in hdr,
                FIELDS_NEW in hdr, DRAW_NEW in cpp,
                cpp.count(LOOP_NEW) == 2)
    if all(complete):
        return files, False
    if any(complete):
        raise ValueError("Partially installed Hulk shared scene wait budget")
    if "HULK/SCENE-PIPELINE-WAIT" not in renderer:
        raise ValueError("Install Hulk scene pipeline wait first")
    if cpp.count(LOOP_OLD) != 2:
        raise ValueError("Upstream D101 changed two shader/pipeline wait loops")
    hdr = once(hdr, INC_OLD, INC_NEW, "chrono header")
    hdr = once(hdr, HEADER_OLD, HEADER_NEW, "SetWaitForPipelines API")
    hdr = once(hdr, FIELDS_OLD, FIELDS_NEW, "scene wait fields")
    cpp = once(cpp, DRAW_OLD, DRAW_NEW, "per-draw deadline")
    cpp = cpp.replace(LOOP_OLD, LOOP_NEW)
    renderer = once(renderer, RENDER_OLD, RENDER_NEW, "scene wait caller")
    return {"src/renderer.cpp": renderer, "src/draw.h": hdr,
            "src/draw.cpp": cpp}, True

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_scene_wait_budget.py NATIVE_X_ROOT")
    root = Path(sys.argv[1]).resolve()
    rev = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if rev != PIN or PIN not in SUPPORTED:
        raise ValueError("Unreviewed NativeXRenderer revision: " + rev)
    paths = ("src/renderer.cpp", "src/draw.h", "src/draw.cpp")
    src = {path: (root / path).read_text(encoding="utf-8") for path in paths}
    dst, changed = patch(src)
    if changed:
        for path in paths:
            (root / path).write_text(dst[path], encoding="utf-8", newline="\n")
    print("Hulk main-scene bounded shader/pipeline wait:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
