#!/usr/bin/env python3
"""Track LAST executed Xenos pixel IM_LOAD, not a cached program's first load.

Log192 GPU verifies 49 pixel-program mismatches, first 24 on consecutive
DrawIndexedVertices #1819..1842. NativeX Program::loaded_by and ::packet
describe the first insertion into the shared shader cache, not necessarily
the latest IM_LOAD. Do not infer the latest program load from cached metadata.
This patch records actual per-state-tracker shader load packet and serial,
including when the cache fast path is used. No shader or GPU behavior changes.
"""
from pathlib import Path
import os
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
                     "bf965e952c98e050753e91d08f32685a9f754b12")
SUPPORTED = {"bf965e952c98e050753e91d08f32685a9f754b12",
             "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}
HEADER_GETTER_OLD = "  const Program* pixel_program_ptr() const { return programs_[1]; }"
HEADER_GETTER_NEW = HEADER_GETTER_OLD + """
  // HULK/PS-RECENT-LOAD: actual last pixel IM_LOAD processed by this tracker.
  // Program::packet/loaded_by may refer to the shader cache's FIRST use.
  uint32_t hulk_ps_recent_packet() const { return hulk_ps_recent_packet_; }
  uint32_t hulk_ps_recent_call() const { return hulk_ps_recent_call_; }
  uint64_t hulk_ps_recent_serial() const { return hulk_ps_recent_serial_; }"""
HEADER_FIELD_OLD = "  std::array<const Program*, 2> programs_{};"
HEADER_FIELD_NEW = HEADER_FIELD_OLD + """
  uint32_t hulk_ps_recent_packet_ = 0;
  uint32_t hulk_ps_recent_call_ = 0;
  uint64_t hulk_ps_recent_serial_ = 0;"""
LOAD_OLD = """  for (const auto& load : scan_.shader_loads) {
    if (load.type >= 2) continue;"""
LOAD_NEW = """  for (const auto& load : scan_.shader_loads) {
    if (load.type >= 2) continue;
    // HULK/PS-RECENT-LOAD: execute before the shader cache's fast path.
    // This must run BEFORE the recent_programs_ fast-path continue and
    // cached-program lookup: both reuse Program's first-origin metadata.
    if (load.type == 1) {
      hulk_ps_recent_packet_ = load.packet;
      hulk_ps_recent_call_ = uint32_t(call);
      ++hulk_ps_recent_serial_;
    }"""
RESET_OLD = """  programs_ = {};
  record_.Clear();"""
RESET_NEW = """  programs_ = {};
  hulk_ps_recent_packet_ = 0;
  hulk_ps_recent_call_ = 0;
  hulk_ps_recent_serial_ = 0;
  record_.Clear();"""
# NativeX D139/D140 already resets its upstream loaded_at_ memo.
# Keep that reset in the Hulk PS provenance patch, not replace/erase it.
RESET_D140_OLD = """  programs_ = {};
  loaded_at_ = {};
  record_.Clear();"""
RESET_D140_NEW = """  programs_ = {};
  loaded_at_ = {};
  hulk_ps_recent_packet_ = 0;
  hulk_ps_recent_call_ = 0;
  hulk_ps_recent_serial_ = 0;
  record_.Clear();"""
LOG_OLD = """                "ps={:016x} vs={:016x} load_call={} packet={:#x} "
                "address={:#x} words={} immediate={}","""
LOG_NEW = """                "ps={:016x} vs={:016x} cache_first_call={} cache_first_packet={:#x} "
                "address={:#x} words={} immediate={} "
                "last_load_call={} last_load_packet={:#x} last_load_serial={}","""
ARGS_OLD = """                hulk_ps ? hulk_ps->ucode.size() : size_t(0),
                hulk_ps && hulk_ps->immediate);"""
ARGS_NEW = """                hulk_ps ? hulk_ps->ucode.size() : size_t(0),
                hulk_ps && hulk_ps->immediate,
                tracker.hulk_ps_recent_call(), tracker.hulk_ps_recent_packet(),
                tracker.hulk_ps_recent_serial());"""

def once(s: str, old: str, new: str, context: str) -> str:
    if s.count(old) != 1:
        raise ValueError(f"Hulk PS recent-load {context} anchor count={s.count(old)}")
    return s.replace(old, new, 1)

def patch(files: dict[str, str]) -> tuple[dict[str, str], bool]:
    paths = {"src/state.cpp", "src/state.h", "src/renderer.cpp"}
    if set(files) != paths:
        raise ValueError("Need exactly state.cpp, state.h and renderer.cpp")
    cpp, header, renderer = (files[k] for k in
                             ("src/state.cpp", "src/state.h", "src/renderer.cpp"))
    done = (HEADER_GETTER_NEW in header, HEADER_FIELD_NEW in header,
            LOAD_NEW in cpp,
            (RESET_NEW in cpp or RESET_D140_NEW in cpp),
            LOG_NEW in renderer, ARGS_NEW in renderer)
    if all(done):
        return files, False
    if any(done) or "HULK/PS-RECENT-LOAD" in header:
        raise ValueError("Partially installed Hulk recent PS load tracing")
    if "HULK/PS-PROGRAM-ORIGIN" not in renderer:
        raise ValueError("Install read-only PS origin capture before recent load")
    header = once(header, HEADER_GETTER_OLD, HEADER_GETTER_NEW, "getter")
    header = once(header, HEADER_FIELD_OLD, HEADER_FIELD_NEW, "storage")
    cpp = once(cpp, LOAD_OLD, LOAD_NEW, "Gather load")
    if RESET_D140_OLD in cpp:
        cpp = once(cpp, RESET_D140_OLD, RESET_D140_NEW, "D140 memo Reset")
    else:
        cpp = once(cpp, RESET_OLD, RESET_NEW, "D135 Reset")
    renderer = once(renderer, LOG_OLD, LOG_NEW, "log fields")
    renderer = once(renderer, ARGS_OLD, ARGS_NEW, "log values")
    return {"src/state.cpp": cpp, "src/state.h": header,
            "src/renderer.cpp": renderer}, True

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_ps_recent_load.py NativeXRoot")
    root = Path(sys.argv[1]).resolve()
    rev = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if rev != PIN or PIN not in SUPPORTED:
        raise ValueError("Unreviewed NativeX revision " + rev)
    paths = ("src/state.cpp", "src/state.h", "src/renderer.cpp")
    before = {k: (root / k).read_text(encoding="utf-8") for k in paths}
    after, changed = patch(before)
    if changed:
        for k in paths:
            (root / k).write_text(after[k], encoding="utf-8", newline="\n")
    print("Hulk last executed PS IM_LOAD provenance:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
