#!/usr/bin/env python3
"""Serialize Vulkan driver shader/pipeline creation for Hulk's NVIDIA crash.

Log 185 + owner WER minidump: access violation in nvgpucomp64.dll while the
renderer was producing hundreds of shader/pipeline objects. This does not
prove application-level creation concurrency caused the driver failure.
A single optional mutex limits Vulkan device CREATE calls across the
renderer shader workers and the pipeline path. It does not touch game data,
shader bytecode, guest render state, or GPU command execution.
"""
from __future__ import annotations
import os
from pathlib import Path
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
                     "bf965e952c98e050753e91d08f32685a9f754b12")
PINS = {"bf965e952c98e050753e91d08f32685a9f754b12",
        "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}
HEADER = """#pragma once
// HULK/DRIVER-CREATE-GATE: ONE cross-translation-unit Vulkan creation gate.
#include <mutex>
namespace native_x_renderer {
inline std::mutex& HulkDriverCompileMutex() {
  static std::mutex mutex;
  return mutex;
}
}  // namespace native_x_renderer
"""
CVAR = """REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_serial_driver_creation, true,
                    "NativeXRenderer",
                    "Hulk: serialize shader and pipeline creation calls after NVIDIA compiler crash");
"""
SHADER_CALL = "  device_->CreateShader(create_info, &job.host->shader);"
PIPELINE_CALL = "  device_->CreateGraphicsPipelineState(create_info, &pipeline->pso);"
SHADER_LOCK = """  // HULK/DRIVER-CREATE-GATE: serialize only host driver object creation,
  // never shader translation or GPU execution. Optional for A/B diagnosis.
  {
    std::unique_lock<std::mutex> hulk_driver_gate(HulkDriverCompileMutex(),
                                                  std::defer_lock);
    if (REXCVAR_GET(native_x_renderer_hulk_serial_driver_creation))
      hulk_driver_gate.lock();
    device_->CreateShader(create_info, &job.host->shader);
  }
"""
PIPELINE_LOCK = """  // HULK/DRIVER-CREATE-GATE: same mutex as ShaderCache worker threads.
  {
    std::unique_lock<std::mutex> hulk_driver_gate(HulkDriverCompileMutex(),
                                                  std::defer_lock);
    if (REXCVAR_GET(native_x_renderer_hulk_serial_driver_creation))
      hulk_driver_gate.lock();
    device_->CreateGraphicsPipelineState(create_info, &pipeline->pso);
  }
"""

def patch(shaders: str, pipelines: str) -> tuple[str, str, bool]:
    is_sh = shaders.count("HULK/DRIVER-CREATE-GATE") == 2
    is_pl = pipelines.count("HULK/DRIVER-CREATE-GATE") == 1
    if is_sh and is_pl:
        if (SHADER_LOCK not in shaders or PIPELINE_LOCK not in pipelines or
            CVAR not in shaders or
            "REXCVAR_DECLARE(bool, native_x_renderer_hulk_serial_driver_creation);" not in pipelines):
            raise ValueError("Incomplete Hulk driver gate")
        return shaders, pipelines, False
    if is_sh or is_pl or "HulkDriverCompileMutex" in shaders or "HulkDriverCompileMutex" in pipelines:
        raise ValueError("Partially applied Hulk driver creation gate")
    if "HULK/NVIDIA-COMPILE-BOUNDARY" not in shaders or "HULK/NVIDIA-COMPILE-BOUNDARY" not in pipelines:
        raise ValueError("NVIDIA compiler boundary diagnostics must be installed first")
    if shaders.count('#include "shaders.h"\n') != 1 or \
       pipelines.count('#include "pipelines.h"\n') != 1 or \
       shaders.count('#include <rex/logging.h>\n') != 1 or \
       pipelines.count('#include <xxhash.h>\n') != 1 or \
       shaders.count(SHADER_CALL) != 2 or \
       pipelines.count(PIPELINE_CALL) != 1:
        raise ValueError("NativeX driver creation source anchors drifted")
    shaders = shaders.replace('#include "shaders.h"\n',
                              '#include "shaders.h"\n#include "hulk_compiler_gate.h"\n', 1)
    shaders = shaders.replace('#include <rex/logging.h>\n',
                              '#include <rex/logging.h>\n#include <rex/cvar.h>\n', 1)
    shaders = shaders.replace('namespace native_x_renderer {\n',
                              CVAR + '\nnamespace native_x_renderer {\n', 1)
    shaders = shaders.replace(SHADER_CALL, SHADER_LOCK)
    pipelines = pipelines.replace('#include "pipelines.h"\n',
                                  '#include "pipelines.h"\n#include "hulk_compiler_gate.h"\n', 1)
    pipelines = pipelines.replace('#include <xxhash.h>\n',
        '#include <xxhash.h>\n\nREXCVAR_DECLARE(bool, native_x_renderer_hulk_serial_driver_creation);\n', 1)
    pipelines = pipelines.replace(PIPELINE_CALL, PIPELINE_LOCK, 1)
    return shaders, pipelines, True

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_driver_compile_serial.py NativeXRoot")
    root = Path(sys.argv[1]).resolve()
    actual = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if PIN not in PINS or actual != PIN:
        raise ValueError("Unreviewed NativeX source revision " + actual)
    a, b = root / "src/shaders.cpp", root / "src/pipelines.cpp"
    hdr = root / "src/hulk_compiler_gate.h"
    if hdr.exists() and hdr.read_text(encoding="utf-8") != HEADER:
        raise ValueError("Existing incompatible Hulk driver gate header")
    s, p, changed = patch(a.read_text(encoding="utf-8"),
                          b.read_text(encoding="utf-8"))
    if changed:
        if hdr.exists(): raise ValueError("Partial Hulk driver gate header")
        a.write_text(s, encoding="utf-8", newline="\n")
        b.write_text(p, encoding="utf-8", newline="\n")
        hdr.write_text(HEADER, encoding="utf-8", newline="\n")
    elif not hdr.exists():
        raise ValueError("Driver gate code present without header")
    print("Hulk NVIDIA driver create gate:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
