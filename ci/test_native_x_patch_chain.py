#!/usr/bin/env python3
"""NativeX integration gate: replay the COMPLETE actual Windows patch chain.

This tests the exact upstream pinned source before private XEX/codegen and the
long 800-object Windows compile. No game binaries, private data or public
build artifacts are involved.
"""
from pathlib import Path
import os
import py_compile
import re
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
BUILDER=ROOT/"native_port"/"BUILD_WINDOWS.ps1"
SHA="bf965e952c98e050753e91d08f32685a9f754b12"
CANDIDATE_SHA="919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"  # D127-D135: source-only migration gate, NOT the production renderer
EXPERIMENTS=[
 "ci/test_patch_native_x_hulk_packet_first.py",
 "ci/patch_native_x_hulk_packet_first.py",
 "ci/test_patch_native_x_hulk_draw_write_ownership.py",
 "ci/patch_native_x_hulk_draw_write_ownership.py",
 "ci/test_patch_native_x_hulk_pm4_draw_payload.py",
 "ci/patch_native_x_hulk_pm4_draw_payload.py",
 "ci/test_patch_native_x_hulk_packet_dma_snapshot.py",
 "ci/patch_native_x_hulk_packet_dma_snapshot.py",
 "ci/test_patch_native_x_hulk_texture_resource_guard.py",
 "ci/patch_native_x_hulk_texture_resource_guard.py",
 "ci/test_patch_native_x_hulk_spirv_compiler_guard.py",
 "ci/patch_native_x_hulk_spirv_compiler_guard.py",
]

def commands():
    # Read the exact production order from the only Windows build script.
    # Hulk's PM4 patches contain DIGITS ("pm4"), so [a-z_]+ silently omitted
    # essential patches and made #30's preflight fail at gap_draw.
    names=[]
    for line in BUILDER.read_text(encoding="utf-8").splitlines():
        if "Invoke-Checked 'python'" not in line or "patch_native_x_hulk_" not in line:
            continue
        match=re.search(r"'(ci/(?:test_)?patch_native_x_hulk_[a-z0-9_]+\.py)'", line)
        if not match:
            raise RuntimeError("Unparsed NativeX build step: " + line.strip())
        names.append(match.group(1))
    if len(names) < 39 or len(names) != len(set(names)):
        raise RuntimeError(f"NativeX production patch manifest incomplete/duplicated: {len(names)}")
    for name in EXPERIMENTS:
        if name not in names:
            raise RuntimeError(f"NativeX patch missing from real Windows build: {name}")
    for first, second in (
        ("ci/patch_native_x_hulk_pm4_gap.py", "ci/patch_native_x_hulk_gap_draw.py"),
        ("ci/patch_native_x_hulk_gap_stream.py", "ci/patch_native_x_hulk_pm4_draw_payload.py"),
        ("ci/patch_native_x_hulk_pm4_draw_payload.py", "ci/patch_native_x_hulk_frame_path_probe.py"),
        ("ci/patch_native_x_hulk_packet_surfaces.py", "ci/patch_native_x_hulk_packet_dma_snapshot.py"),
        ("ci/patch_native_x_hulk_packet_dma_snapshot.py", "ci/patch_native_x_hulk_surface_miss_trace.py"),
        ("ci/patch_native_x_hulk_packet_surfaces.py", "ci/patch_native_x_hulk_surface_fallback.py"),
        ("ci/patch_native_x_hulk_surface_fallback.py", "ci/patch_native_x_hulk_packet_first.py"),
        ("ci/patch_native_x_hulk_packet_first.py", "ci/patch_native_x_hulk_draw_write_ownership.py"),
        ("ci/patch_native_x_hulk_unbound_mrt.py", "ci/patch_native_x_hulk_surface_miss_trace.py"),
    ):
        if first not in names or second not in names or names.index(first) >= names.index(second):
            raise RuntimeError(f"NativeX patch ordering incorrect: {first} before {second}")
    return names

def checked(args,cwd=ROOT):
    result=subprocess.run(args,cwd=cwd,stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT,text=True,errors="replace")
    if result.returncode:
        print(result.stdout[-7000:],file=sys.stderr)
        raise RuntimeError(f"Source-only NativeX preflight failed: {Path(args[1]).name}")
    return result.stdout

def run(sha=SHA):
    if sha not in (SHA, CANDIDATE_SHA):
        raise ValueError("NativeX revision must be one of the two explicitly audited pins")
    # Every separate Hulk patch script compares its *actual* checkout HEAD
    # with this value. The candidate is not disguised as the old revision.
    os.environ["HULK_NATIVEX_SOURCE_SHA"] = sha
    print(f"[NativeX/preflight] source SHA={sha}; mode={'candidate-only' if sha != SHA else 'production'}", flush=True)
    scripts=commands()
    # Fail quickly on syntax errors before a checkout or any private compilation.
    for name in scripts:
        py_compile.compile(str(ROOT/name), doraise=True)
    with tempfile.TemporaryDirectory(prefix="hulk-nxr-patch-") as directory:
        source=Path(directory)/"NativeXRenderer"
        checked(["git","clone","--no-checkout","--depth","1",
                 "https://github.com/ElGuslml/NativeXRenderer.git",str(source)])
        checked(["git","-C",str(source),"fetch","--depth","1","origin",sha])
        checked(["git","-C",str(source),"checkout","--detach",sha])
        for name in scripts:
            file=ROOT/name
            command=[sys.executable,str(file)]
            if not name.startswith("ci/test_"): command.append(str(source))
            checked(command)
            print("[NativeX/preflight] OK",name,flush=True)
        renderer=(source/"src/renderer.cpp").read_text(encoding="utf-8")
        draw=(source/"src/draw.cpp").read_text(encoding="utf-8")
        needed=["HULK/PM4-REGISTER-ONLY","const bool submitted = g_draw->Draw(",
                "HULK/PM4-SURFACE","HULK/MSAA-GRID","HULK/PM4-SAMPLE-VIEW","HULK/PM4-HISTORY-ALIAS","HULK/PM4-ACCEPT","HULK/RESOLVE-SCALE","HULK/REVERSE-ALIAS","HULK/UNBOUND-MRT"]
        missing=[mark for mark in needed if mark not in renderer]
        if "bool DrawPath::Draw(" not in draw:missing.append("bool DrawPath::Draw(")
        if ("live.depth == (slot == 4)" not in renderer or
            "const HulkSurfaceIdentity xdk_key{2u * (gpu_surface & 0x3FFFu), gpu_info & 0x000F0FFFu};" not in renderer or
            "live.width == known.width && live.height == known.height" not in renderer or
            "bound.pitch() == 2u * (gpu_surface & 0x3FFFu)" not in renderer):
            missing.append("HULK 520/1040 depth+color MSAA alias requires matching live XDK object")
        if "hulk_packet_draws_mapped" not in renderer:
            missing.append("HULK accepted surface binding counter")
        texture=(source/"src/guest_textures.cpp").read_text(encoding="utf-8")
        if "HULK/TEXTURE-RESOURCE-GUARD" not in texture:
            missing.append("Hulk Vulkan texture descriptor guard")
        state=(source/"src/state.cpp").read_text(encoding="utf-8")
        for name, body in (
            ("HULK/PM4-PAYLOAD",state),
            ("HULK/FETCH-WRITER",state),
            ("HULK/PM4-DMA-SNAPSHOT",renderer),
            ("HULK/FETCH-ORIGIN",draw),
            ("HULK/DMA-ORIGIN",draw),
        ):
            if name not in body:missing.append(name)
        if sha == CANDIDATE_SHA:
            targets=(source/"src/targets.cpp").read_text(encoding="utf-8")
            if "native ? kNativeScale : scale_" not in targets or "&& !native &&" not in targets:
                missing.append("D127 native-scale target preserved by Hulk MSAA grid")
            if "source->scale.x != target->scale.x" not in renderer:
                missing.append("D127 scaled resolves preserved")
        # Hulk #57 preflight: verify the *actual integrated C++*, not only
        # independent source-replacement fixtures. These guards are required
        # by build 56's malformed DMA, invalid primitive, stale RT and 4x
        # depth traces. A compile alone cannot prove the GPU frame is correct.
        for marker, contents in (
            ("HULK/PM4-DMA-CANCEL", renderer),
            ("HULK/DIRECT-DMA-SNAPSHOT", renderer),
            ("HULK/DMA-RESTORE-AFTER-AHEAD", draw),
            ("HULK/PM4-PRIMITIVE-SOURCE", renderer),
            ("HULK/PM4-DMA-ADDRESS-PARITY", renderer),
            ("HULK/PM4-PERSISTENT-DMA", renderer),
            ("HULK/PM4-HISTORY-LIVE", renderer),
            ("HULK/PM4-RT0-1X-REBASE", renderer),
            ("HULK/PM4-DEPTH-REBASE-4X", renderer),
            ("HULK/PM4-REGISTER-ORDER", state),
            ("HULK/PM4-SHADER-BOUNDS", state),
        ):
            if marker not in contents:
                missing.append("missing critical integrated C++: " + marker)
        # Rejected DMA Draws must NOT be queued as Command::Type::kDraw:
        # NativeX Capture::~Capture() publishes even after an early return.
        rejected = renderer.find(
            "!CapturePacketDmaIndices(tracker, base, c))")
        if rejected < 0:
            missing.append("DMA snapshot capture call missing")
        else:
            cancel = renderer[rejected:rejected + 850]
            if ("c.type = Command::Type::kNone;" not in cancel or
                    cancel.index("c.type = Command::Type::kNone;") >
                    cancel.rfind("return;")):
                missing.append("DMA rejected capture still publishes a Draw")
        if missing:raise RuntimeError(f"NativeX patch chain markers absent: {missing}")
        print(f"[NativeX/preflight] All {len(scripts)} real NativeX {sha[:8]} source patches pass")
if __name__=="__main__":
    if len(sys.argv) > 2 or (len(sys.argv) == 2 and sys.argv[1] != "--candidate"):
        raise SystemExit("Usage: python ci/test_native_x_patch_chain.py [--candidate]")
    run(CANDIDATE_SHA if len(sys.argv) == 2 else SHA)
