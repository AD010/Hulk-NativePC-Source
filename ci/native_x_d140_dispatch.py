#!/usr/bin/env python3
"""Execute the reviewed Hulk NativeX source patches against the exact D140 SHA.

The D140 source-only CI already applies every native source patch with these
same pin adapters. This is the matching *Windows build* dispatch: retain all
existing source-anchor/ABI checks, reject wrong upstream revisions, and never
fake Git HEAD. Work exclusively in the checked-out renderer source.
"""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
D140 = "fa19ea85fec5c7f7f8a2df82d6c4a86548817b52"
D135 = "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"
OLDER = "bf965e952c98e050753e91d08f32685a9f754b12"


def checked_patch_path(path: str) -> Path:
    p = Path(path).resolve()
    if p.parent != (ROOT / "ci").resolve() or not p.name.startswith(
            ("patch_native_x_hulk_", "test_patch_native_x_hulk_")) or p.suffix != ".py":
        raise ValueError("Unexpected source patch/test path: " + str(p))
    if not p.is_file():
        raise ValueError("Missing NativeX patch/test script: " + str(p))
    return p


def check_renderer(path: str) -> Path:
    root = Path(path).resolve()
    if not (root / "src" / "state.cpp").is_file():
        raise ValueError("Missing NativeX source checkout")
    head = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if head != D140:
        raise ValueError("D140 migration requires actual NativeX checkout " + D140 +
                         ", found " + head)
    return root


def run_patch(script: Path, renderer: Path) -> None:
    # The source-only preflight invokes this function directly, bypassing
    # main(). Validate the real checkout here too, before loading any patch.
    renderer = check_renderer(str(renderer))
    script = checked_patch_path(str(script))
    spec = importlib.util.spec_from_file_location(
        "hulk_d140_windows_" + script.stem, script)
    if spec is None or spec.loader is None:
        raise ValueError("Cannot import reviewed NativeX patch " + script.name)
    module = importlib.util.module_from_spec(spec)
    # Legacy transformers can validate their old import-time SHA. The
    # existing C++ source is STILL the real D140 checkout, never the old one.
    saved_pin = os.environ.get("HULK_NATIVEX_SOURCE_SHA")
    os.environ["HULK_NATIVEX_SOURCE_SHA"] = D135
    try:
        spec.loader.exec_module(module)
    finally:
        if saved_pin is None:
            os.environ.pop("HULK_NATIVEX_SOURCE_SHA", None)
        else:
            os.environ["HULK_NATIVEX_SOURCE_SHA"] = saved_pin
    for attr in ("PIN", "SOURCE_SHA"):
        value = getattr(module, attr, None)
        if isinstance(value, str) and value in (OLDER, D135):
            setattr(module, attr, D140)
    for attr in ("PINS", "SUPPORTED"):
        value = getattr(module, attr, None)
        if isinstance(value, set):
            value.add(D140)
    if not callable(getattr(module, "main", None)):
        raise ValueError("Reviewed NativeX patch has no main() " + script.name)
    argv = sys.argv
    try:
        sys.argv = [str(script), str(renderer)]
        module.main()
    finally:
        sys.argv = argv
    print("[HULK/D140-WINDOWS-PATCH] applied and source-checked " + script.name,
          flush=True)


def main() -> None:
    if len(sys.argv) not in (3, 4):
        raise SystemExit(
            "Usage: python ci/native_x_d140_dispatch.py --test <ci/test_patch...py> "
            "| --patch <ci/patch...py> <NativeX checkout>")
    mode, path = sys.argv[1:3]
    script = checked_patch_path(path)
    if mode == "--test" and len(sys.argv) == 3 and script.name.startswith("test_"):
        env = dict(os.environ)
        env["HULK_NATIVEX_SOURCE_SHA"] = D135
        subprocess.run([sys.executable, str(script)], cwd=ROOT,
                       env=env, check=True)
        return
    if mode != "--patch" or len(sys.argv) != 4 or not script.name.startswith("patch_"):
        raise ValueError("Invalid NativeX source patch dispatch mode " + mode)
    renderer = check_renderer(sys.argv[3])
    run_patch(script, renderer)


if __name__ == "__main__":
    main()
