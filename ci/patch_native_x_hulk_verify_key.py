#!/usr/bin/env python3
"""Hulk NativeX observer key conflict fix: F10 PC menu vs SDK F10 verify.

Upstream observer.cpp uses F10 for verify, but Hulk owns F10 for the real PC
menu with a working Exit to Windows button. Keep Trace on F9 and verify on
F11, preserving the actual Render/Observer code unchanged.
"""
from pathlib import Path
import subprocess
import sys

PIN = "bf965e952c98e050753e91d08f32685a9f754b12"
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
    if changed:
        target.write_text(output, encoding="utf-8", newline="\n")
    print("Hulk NativeX Observer hotkey F11:", "patched" if changed else "already patched")

if __name__ == "__main__":
    main()
