#!/usr/bin/env python3
"""Native GPU failure telemetry is embedded in the actual game log."""
from pathlib import Path
p=Path(__file__).resolve().parents[1]
src=(p/"native_port/src/native_gpu_autodiag.cpp").read_text()
app=(p/"native_port/src/incredible_hulk_app.h").read_text()
cm=(p/"native_port/CMakeLists.txt").read_text()
for s in ["rex::AddSink(sink)","getCompTexLOD requires pixel shader implicit derivatives",
          "[HULK/PM4-DMA-REJECT]","analysis failed:","frame interval ",
          "GPU ","worst_GPU_average_ms", "real_EoR_Vulkan_scene_NOT_YET_VERIFIED"]:
    assert s in src,s
assert "InstallAutomaticGpuDiagnostics();" in app
assert "ReportAutomaticGpuDiagnostics();" in app
assert "src/native_gpu_autodiag.cpp" in cm
