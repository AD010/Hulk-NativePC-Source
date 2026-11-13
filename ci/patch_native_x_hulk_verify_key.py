#!/usr/bin/env python3
"""Hulk NativeX observer key conflict fix: F10 PC menu vs SDK F10 verify.

Upstream observer.cpp uses F10 for verify, but Hulk owns F10 for the real PC
menu with a working Exit to Windows button. Keep Trace on F9 and verify on
F11, and compare raw float registers when shader usage analysis is unavailable.
This changes diagnostics only, not rendering.
"""
from pathlib import Path
import subprocess
import sys

import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
OLD_BIND = 'rex::ui::RegisterBind("bind_native_x_renderer_verify", "F10",'
NEW_BIND = 'rex::ui::RegisterBind("bind_native_x_renderer_verify", "F11",'
OLD_LOG = 'REXLOG_INFO("[native_x_renderer] verify on: F10 (bind_native_x_renderer_verify)'
NEW_LOG = 'REXLOG_INFO("[native_x_renderer] verify on: F11 (bind_native_x_renderer_verify)'


def patch_text(source: str) -> tuple[str, bool]:
    if source.count(NEW_BIND) == 1 and source.count(NEW_LOG) == 1:
        return source, False
    if source.count(OLD_BIND) != 1 or source.count(OLD_LOG) != 1:
        raise ValueError("Pinned NativeX observer key binding anchors mismatch")
    return source.replace(OLD_BIND, NEW_BIND).replace(OLD_LOG, NEW_LOG), True



# A missing SDK shader analysis is normal in sync-only mode. It must not
# silently remove every float register from the verifier's comparison.
CONSTANTS_OLD = """    if (!map) return;
    for (uint32_t c = 0; c < 256; ++c) {
      if (!map->float_dynamic_addressing && !Bit(map->float_bitmap, c)) continue;"""
CONSTANTS_NEW = """    // Without usage metadata compare the entire raw register window. A
    // mismatch then means state divergence, not proof the shader reads it.
    for (uint32_t c = 0; c < 256; ++c) {
      if (map && !map->float_dynamic_addressing && !Bit(map->float_bitmap, c)) continue;"""
COVERAGE_REPLACEMENTS = (
    (CONSTANTS_OLD, CONSTANTS_NEW),
    ('    ++g_unanalyzed;', '    // Count missing analysis for either shader below.'),
    ('  // Float constants the shaders read, in the windows SQ_VS_CONST / SQ_PS_CONST select.',
     '  if (!vs_map || !ps_map) ++g_unanalyzed;\n'
     '  // Shader-used float constants, or full raw windows when usage is unknown.'),
    ('draw calls fully match the rebuilt state',
     'draw calls have no mismatches in compared state (not full shader validation)'),
    ('not matched; shaders without analysis: {}',
     'not matched; executions missing VS or PS analysis: {}. Missing analysis uses raw float windows; '
     'unused-register differences are not proof of a rendering defect. Zero shader hashes are unverified'),
)


def patch_coverage(source: str) -> tuple[str, bool]:
    if all(source.count(new) == 1 for _, new in COVERAGE_REPLACEMENTS):
        return source, False
    for old, _ in COVERAGE_REPLACEMENTS:
        if source.count(old) != 1:
            raise ValueError("Pinned NativeX observer coverage anchor mismatch: " + old[:80])
    for old, new in COVERAGE_REPLACEMENTS:
        source = source.replace(old, new)
    return source, True


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_verify_key.py <NativeXRenderer checkout>")
    folder = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(
        ["git", "-C", str(folder), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN:
        raise ValueError(f"Expected NativeX {PIN}, got {sha}")
    target = folder / "src" / "observer.cpp"
    output, changed = patch_text(target.read_text(encoding="utf-8"))
    output, coverage_changed = patch_coverage(output)
    changed = changed or coverage_changed
    if changed:
        target.write_text(output, encoding="utf-8", newline="\n")
    print("Hulk NativeX Observer F11 and raw constant coverage:", "patched" if changed else "already patched")

if __name__ == "__main__":
    main()
