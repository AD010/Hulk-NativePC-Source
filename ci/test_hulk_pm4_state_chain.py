#!/usr/bin/env python3
"""Reproduce the complete Hulk *state/PM4* patch order on a pinned NativeX source.

This is an offline, source-only integration gate, not a Vulkan/game test.
It applies original Hulk source patch modules in the same order as
native_port/BUILD_WINDOWS.ps1, then the staged PFP BIN extension. The upstream
directory is READ ONLY; output lives only in a temporary directory.
Usage:
  python ci/test_hulk_pm4_state_chain.py <pristine NativeXRenderer checkout>
"""
from __future__ import annotations
import subprocess
import sys
import tempfile
from pathlib import Path

from patch_native_x_hulk_pm4_gap import patch_header, patch_cpp
from patch_native_x_hulk_gap_draw import PATCHES as DRAW_PATCHES, transform as draw_transform
from patch_native_x_hulk_gap_stream import PATCHES as STREAM_PATCHES, transform as stream_transform
from patch_native_x_hulk_pm4_draw_payload import patch_text as payload_transform
from patch_native_x_hulk_bin_predication import transform as pfp_transform
from patch_native_x_hulk_register_order import transform as register_order_transform


def apply_state_chain(files: dict[str, str]) -> dict[str, str]:
    """Source order is material: later patches depend on earlier anchors."""
    result = dict(files)
    if set(result) != {"src/state.cpp", "src/state.h"}:
        raise ValueError("Require exactly the NativeX state.cpp and state.h")
    result["src/state.cpp"], changed_cpp = patch_cpp(result["src/state.cpp"])
    result["src/state.h"], changed_h = patch_header(result["src/state.h"])
    if changed_cpp != changed_h:
        raise ValueError("PM4 gap tracker source/header disagree")

    for patch, transform in ((DRAW_PATCHES, draw_transform),
                             (STREAM_PATCHES, stream_transform)):
        for path in ("src/state.cpp", "src/state.h"):
            if path in patch:
                result[path], did_change = transform(result[path], patch[path])
                if not did_change:
                    raise ValueError(f"{path}: {transform.__module__} unexpectedly already applied")
    result["src/state.cpp"], did_change = payload_transform(result["src/state.cpp"])
    if not did_change:
        raise ValueError("PM4 payload guard unexpectedly already installed")
    result = pfp_transform(result)

    required_cpp = (
        "HULK/PFP-PREDICATE", "HULK/PFP-RESET", "HULK/PFP-BIN",
        "HULK/PM4-PACKET-BOUNDS", "HULK/PM4-SHADER-BOUNDS",
        "if (copy_microcode || immediate)", "hulk_bin_mask_at_gap",
        "if (on_packet_draw && out.complete) on_packet_draw();",
        "const bool stream_gap = scan_.complete &&",
        "const bool single_direct_draw = scan_.complete &&",
        "rejected incomplete PM4 state before Draw/Gather",
    )
    absent = [marker for marker in required_cpp if marker not in result["src/state.cpp"]]
    if absent:
        raise ValueError("Combined NativeX PM4 source missing: " + ", ".join(absent))
    assert "uint64_t bin_mask = 0xFFFFFFFFull" in result["src/state.h"]
    if pfp_transform(result) != result:
        raise ValueError("PFP transform is not idempotent")
    return result


def verify_existing_build_order() -> None:
    # The actual Windows build is kept untouched. Guard source-level tests
    # against future changes that reorder prerequisite PM4 patches.
    build = Path(__file__).resolve().parent.parent / "native_port" / "BUILD_WINDOWS.ps1"
    manifest = build.read_text(encoding="utf-8")
    names = (
        "patch_native_x_hulk_pm4_gap.py",
        "patch_native_x_hulk_gap_draw.py",
        "patch_native_x_hulk_gap_stream.py",
        "patch_native_x_hulk_pm4_draw_payload.py",
    )
    order = [manifest.find(name) for name in names]
    if any(i < 0 for i in order) or order != sorted(order):
        raise ValueError("NativeX PM4 build prerequisites changed order: " + repr(order))


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    verify_existing_build_order()
    root = Path(sys.argv[1]).resolve()
    files = {name: (root / name).read_text(encoding="utf-8")
             for name in ("src/state.cpp", "src/state.h")}
    processed = apply_state_chain(files)
    with tempfile.TemporaryDirectory(prefix="hulk_pm4_") as tmp:
        output = Path(tmp)
        (output / "src").mkdir()
        for name, content in processed.items():
            (output / name).write_text(content, encoding="utf-8", newline="\n")
        test = Path(__file__).with_name("test_patch_native_x_hulk_bin_cpp_scan.py")
        subprocess.run([sys.executable, str(test), str(output)],
                       check=True)
    # The C++ scan validation intentionally runs on PFP pre-ordered source:
    # the standalone test replaces ScanDraw with its own test double. Now
    # validate that the SAME verified stream also accepts ordered registers.
    ordered = register_order_transform(processed)
    if "HULK/PM4-REGISTER-ORDER" not in ordered["src/state.cpp"]:
        raise ValueError("Chronological PM4 register patch missing")
    if "std::vector<uint32_t> register_order;" not in ordered["src/state.h"]:
        raise ValueError("Chronological PM4 register header missing")
    if register_order_transform(ordered) != ordered:
        raise ValueError("PM4 register patch not idempotent")
    subprocess.run([sys.executable, str(Path(__file__).with_name(
        "test_patch_native_x_hulk_register_order.py"))], check=True)
    print("PASS: Hulk gap -> draw -> stream -> payload -> PFP -> ordered registers")


if __name__ == "__main__":
    main()
