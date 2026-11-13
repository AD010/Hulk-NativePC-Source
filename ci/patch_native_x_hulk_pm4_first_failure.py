#!/usr/bin/env python3
"""Record the FIRST exact PM4 parser failure behind Hulk's missing XDK draws.

Log 194 (build 85): >130k fail-closed DrawIndexedVertices, mostly with valid
scan_from..end ranges. Existing diagnostics report bounds but not WHY the
scanner sets PacketScan.complete=false. A source line + packet type/opcode,
remaining bytes and indirect depth discriminate truncated tails, invalid
shader loads and register/guest bounds. This changes diagnostics only:
no incomplete draw is admitted and no shader/guest bytes are exported.

Run LAST among production NativeX source transforms, after the incomplete XDK
draw guard, shader provenance and original observer source-only patches.
"""
from __future__ import annotations
import os
from pathlib import Path
import re
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
    "bf965e952c98e050753e91d08f32685a9f754b12")
PINS = {"bf965e952c98e050753e91d08f32685a9f754b12",
        "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}

HEADER_OLD = """  bool complete = true;  // every byte between the call's start and end was read as packets"""
HEADER_NEW = HEADER_OLD + """
  // HULK/PM4-FIRST-FAILURE: metadata only. Zero site means the scanner did
  // not mark the failure (for example a device ring range failure).
  uint32_t hulk_fail_site = 0;
  uint32_t hulk_fail_packet = 0;
  uint32_t hulk_fail_header = 0;
  uint32_t hulk_fail_depth = 0;
  uint32_t hulk_fail_remaining = 0;"""

SCAN_START = "void ScanPackets("
SCAN_END = "bool FetchSlotDirty("
SCAN_HEAD = """  uint32_t p = begin;
  while (p + 4 <= end) {
    uint32_t header = Load32(base, p);"""
SCAN_HEAD_NEW = """  uint32_t p = begin;
  uint32_t hulk_current_header = 0;
  // HULK/PM4-FIRST-FAILURE: capture the FIRST failing scanner site,
  // including inside nested indirect buffers; don't reset while unwinding.
  auto hulk_note_failure = [&](uint32_t source_line) {
    if (out.complete) {
      out.hulk_fail_site = source_line;
      out.hulk_fail_packet = p >= 4u ? p - 4u : p;
      out.hulk_fail_header = hulk_current_header;
      out.hulk_fail_depth = depth >= 0 ? uint32_t(depth) : 0u;
      out.hulk_fail_remaining = p <= end ? end - p : 0u;
    }
    out.complete = false;
  };
  while (p + 4 <= end) {
    uint32_t header = Load32(base, p);
    hulk_current_header = header;"""

CLEAR_HEAD = "void PacketScan::Clear() {"
CLEAR_OLD = "  complete = true;"
CLEAR_NEW = """  complete = true;
  hulk_fail_site = hulk_fail_packet = hulk_fail_header = 0;
  hulk_fail_depth = hulk_fail_remaining = 0;"""

LOG_OLD = '''                  "writes={} loads={} shader_loads={} - discarded",'''
LOG_NEW = '''                  "writes={} loads={} shader_loads={} "
                  "fail_site={} fail_packet={:#x} fail_header={:#x} "
                  "fail_type={} fail_opcode={:#x} fail_depth={} "
                  "fail_remaining={} - discarded",'''
LOG_ARGS_OLD = """                  scan_.loads.size(), scan_.shader_loads.size());"""
LOG_ARGS_NEW = """                  scan_.loads.size(), scan_.shader_loads.size(),
                  scan_.hulk_fail_site, scan_.hulk_fail_packet,
                  scan_.hulk_fail_header, scan_.hulk_fail_header >> 30,
                  (scan_.hulk_fail_header >> 8) & 0x7fu,
                  scan_.hulk_fail_depth, scan_.hulk_fail_remaining);"""

def exactly_once(source: str, before: str, after: str, label: str) -> str:
    if source.count(before) != 1:
        raise ValueError(f"Hulk PM4 failure origin {label}: anchor count={source.count(before)}")
    return source.replace(before, after, 1)

def patch(files: dict[str, str]) -> tuple[dict[str, str], bool]:
    if set(files) != {"src/state.cpp", "src/state.h"}:
        raise ValueError("Need exactly NativeX state.cpp and state.h")
    cpp, header = files["src/state.cpp"], files["src/state.h"]
    complete = (HEADER_NEW in header, SCAN_HEAD_NEW in cpp,
                CLEAR_NEW in cpp, LOG_NEW in cpp, LOG_ARGS_NEW in cpp)
    if all(complete):
        if "hulk_note_failure(__LINE__)" not in cpp:
            raise ValueError("PM4 diagnostic fields installed without failure sites")
        return files, False
    if any(complete) or "HULK/PM4-FIRST-FAILURE" in cpp + header:
        raise ValueError("Partially installed PM4 failure origin instrumentation")
    if "HULK/INCOMPLETE-XDK-DRAW-GUARD" not in cpp:
        raise ValueError("Install existing safe incomplete XDK draw guard first")
    header = exactly_once(header, HEADER_OLD, HEADER_NEW, "header fields")
    start = cpp.find(SCAN_START)
    end = cpp.find(SCAN_END, start)
    if start < 0 or end <= start or cpp.find(SCAN_START, start + len(SCAN_START)) >= 0:
        raise ValueError("NativeX ScanPackets function bounds ambiguous")
    section = cpp[start:end]
    # Every set-failure expression IN THIS scanner is instrumented without
    # changing its return/branch structure. The first reason wins.
    scanner_failure = re.compile(r"\bout\.complete\s*=\s*false\b")
    errors = len(scanner_failure.findall(section))
    if not 3 <= errors <= 40:
        raise ValueError(f"NativeX scanner failure sites unexpectedly {errors}")
    section = scanner_failure.sub("hulk_note_failure(__LINE__)", section)
    section = exactly_once(section, SCAN_HEAD, SCAN_HEAD_NEW, "scanner header")
    cpp = cpp[:start] + section + cpp[end:]
    clear = cpp.find(CLEAR_HEAD)
    if clear < 0:
        raise ValueError("NativeX PacketScan::Clear missing")
    clear_end = cpp.find("\n}", clear)
    if clear_end < 0 or clear_end - clear > 1000:
        raise ValueError("NativeX PacketScan::Clear shape unexpected")
    block = cpp[clear:clear_end]
    block = exactly_once(block, CLEAR_OLD, CLEAR_NEW, "clear marker")
    cpp = cpp[:clear] + block + cpp[clear_end:]
    cpp = exactly_once(cpp, LOG_OLD, LOG_NEW, "incomplete XDK warning")
    cpp = exactly_once(cpp, LOG_ARGS_OLD, LOG_ARGS_NEW, "warning argument list")
    return {"src/state.cpp": cpp, "src/state.h": header}, True

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_pm4_first_failure.py NativeXRoot")
    root = Path(sys.argv[1]).resolve()
    revision = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        text=True).strip()
    if PIN not in PINS or revision != PIN:
        raise ValueError("Unsupported pinned NativeXRenderer version " + revision)
    paths = ("src/state.cpp", "src/state.h")
    before = {path: (root / path).read_text(encoding="utf-8") for path in paths}
    after, changed = patch(before)
    if changed:
        for path in paths:
            (root / path).write_text(after[path], encoding="utf-8", newline="\n")
    print("Hulk first PM4 parser failure origin:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
