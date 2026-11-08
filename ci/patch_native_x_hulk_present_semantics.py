#!/usr/bin/env python3
"""Hulk NativeX: preserve working logical Swap frame-end, add front-buffer telemetry.

Log 079 confirms Swap at USA 0x82880778, mapped to native logical Present,
finished 31800 GPU frames but displayed black. Log 087 mapped the separate
outer guest Present to native Present: no finished frames, 256 MiB dynamic
heap exhaustion. The Hulk profile therefore maps Swap to NativeX Present
until a real guest frame boundary is verified. Preserve upstream
Present.r4 semantics: here r4 contains the inner Swap front-buffer object.

This pinned source edit ONLY adds sparse read-only Resolve lookup logging.
It does not modify input, shaders, guest XDK code or frame pacing.
"""
from pathlib import Path
import subprocess
import sys

PIN = "bf965e952c98e050753e91d08f32685a9f754b12"
PRESENT_R4 = "  if (call == Call::kPresent) g_front_buffer = ctx.r4.u32;"
OLD = "  ComposePicture(front ? front->srv : nullptr, front ? front->width : 0, front ? front->height : 0, fetch1 & 0x3F,"
NEW = """  // If no matching guest Resolve exists, ComposePicture only clears black.
  // Sample once and every 600 presents, without logging resource contents.
  static uint64_t hulk_present_samples = 0;
  if (++hulk_present_samples == 1 || hulk_present_samples % 600 == 0) {
    REXLOG_INFO("[HULK/NATIVEX] present {} guest_front={:#x} has_fetch={} fetch1={:#x} "
                "resolved={} size={}x{}",
                hulk_present_samples, g_front_buffer, c.has_texture, fetch1,
                front != nullptr && front->srv != nullptr,
                front ? front->width : 0, front ? front->height : 0);
  }
""" + OLD


def patch_text(source: str) -> tuple[str, bool]:
    if source.count(PRESENT_R4) != 1:
        raise ValueError("NativeX Present r4 front setter missing, refusing zero-frame regression")
    if "if (call == Call::kSwap) {" in source:
        raise ValueError("Stale split Swap/Present patch detected")
    if source.count(NEW) == 1:
        return source, False
    if source.count(OLD) != 1:
        raise ValueError("Pinned NativeXRenderer ComposePicture anchor mismatch")
    return source.replace(OLD, NEW), True


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_present_semantics.py <NativeXRenderer checkout>")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN:
        raise ValueError(f"Unexpected NativeXRenderer revision {sha}, expected {PIN}")
    target = root / "src" / "renderer.cpp"
    updated, changed = patch_text(target.read_text(encoding="utf-8"))
    if changed:
        target.write_text(updated, encoding="utf-8", newline="\n")
    print("NativeX Hulk Swap frame-end:", "telemetry patched" if changed else "already patched")


if __name__ == "__main__":
    main()
