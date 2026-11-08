#!/usr/bin/env python3
"""Hulk XDK 6534 NativeX outer Present vs nested Swap calling-convention fix.

Pinned upstream NativeXRenderer assumes its Call::kPresent receives a front
texture in PPC r4. The actual Hulk external Present(dev) does not: the inner
Swap(dev, frontBuffer, params) receives that texture in r4. Capture it there,
even inside the outer call; finish/capture the frame on Present's return.
Also add sparse read-only front-buffer resolve telemetry for black frames.
This does not change gameplay, Raw Input, audio, or guest ABI.
"""
from pathlib import Path
import subprocess
import sys

PIN = "bf965e952c98e050753e91d08f32685a9f754b12"
RULES = [
    (
        "void OnEnter(Call call, PPCContext& ctx, uint8_t* base, bool nested) {\n"
        "  if (call == Call::kClearF) {",
        "void OnEnter(Call call, PPCContext& ctx, uint8_t* base, bool nested) {\n"
        "  // Hulk XDK 6534: a nested Swap(dev, frontBuffer, params) passes the actual\n"
        "  // screen texture in r4. The enclosing Present(dev) has no frontBuffer r4.\n"
        "  // Record Swap even when nested, but ONLY Present ends the output frame.\n"
        "  if (call == Call::kSwap) {\n"
        "    g_front_buffer = ctx.r4.u32;\n"
        "    return;\n"
        "  }\n"
        "  if (call == Call::kClearF) {",
    ),
    (
        "  if (call == Call::kPresent) g_front_buffer = ctx.r4.u32;",
        "  // Do not take the front buffer from Present's unrelated r4 parameter.",
    ),
    (
        "  ComposePicture(front ? front->srv : nullptr, front ? front->width : 0, front ? front->height : 0, fetch1 & 0x3F,",
        "  // Hulk bring-up: black is expected if no matching guest resolve exists.\n"
        "  // Sparse, read-only telemetry distinguishes a missing front texture from\n"
        "  // a resolved texture whose rendered pixels are black. No capture/draw edits.\n"
        "  static uint64_t hulk_present_samples = 0;\n"
        "  if (++hulk_present_samples == 1 || hulk_present_samples % 600 == 0) {\n"
        "    REXLOG_INFO(\"[HULK/NATIVEX] present {} guest_front={:#x} has_fetch={} fetch1={:#x} \"\n"
        "                \"resolved={} size={}x{}\",\n"
        "                hulk_present_samples, g_front_buffer, c.has_texture, fetch1,\n"
        "                front != nullptr && front->srv != nullptr,\n"
        "                front ? front->width : 0, front ? front->height : 0);\n"
        "  }\n"
        "  ComposePicture(front ? front->srv : nullptr, front ? front->width : 0, front ? front->height : 0, fetch1 & 0x3F,",
    ),
]


def patch_text(source: str) -> tuple[str, bool]:
    applied = [new in source for _, new in RULES]
    if all(applied):
        return source, False
    if any(applied):
        raise ValueError("Partially patched NativeXRenderer; refusing mixed revisions")
    for old, new in RULES:
        if old == new or source.count(old) != 1:
            raise ValueError("Pinned NativeXRenderer source anchor mismatch")
    for old, new in RULES:
        source = source.replace(old, new)
    return source, True


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_present_semantics.py <NativeXRenderer checkout>")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN:
        raise ValueError(f"Unexpected NativeXRenderer revision {sha}, expected {PIN}")
    target = root / "src" / "renderer.cpp"
    patched, changed = patch_text(target.read_text(encoding="utf-8"))
    if changed:
        target.write_text(patched, encoding="utf-8", newline="\n")
    print("NativeX Hulk Present/Swap ABI:", "patched" if changed else "already installed")


if __name__ == "__main__":
    main()
