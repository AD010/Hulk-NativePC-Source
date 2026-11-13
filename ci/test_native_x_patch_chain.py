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

# Single authoritative C++ wait snippet: don't copy the 80/200ms value here.
# It previously drifted in #80/#81 and failed complete CI preflight.
from patch_native_x_hulk_scene_wait_budget import DRAW_NEW as HULK_SCENE_WAIT_CPP

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
 "ci/test_patch_native_x_hulk_resolved_resource_guard.py",
 "ci/patch_native_x_hulk_resolved_resource_guard.py",
 "ci/test_patch_native_x_hulk_spirv_compiler_guard.py",
 "ci/patch_native_x_hulk_spirv_compiler_guard.py",
 "ci/test_patch_native_x_hulk_driver_compile_serial.py",
 "ci/patch_native_x_hulk_driver_compile_serial.py",
 "ci/test_patch_native_x_hulk_incomplete_xdk_draw.py",
 "ci/patch_native_x_hulk_incomplete_xdk_draw.py",
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
        ("ci/patch_native_x_hulk_texture_resource_guard.py", "ci/patch_native_x_hulk_spirv_compiler_guard.py"),
        ("ci/patch_native_x_hulk_spirv_compiler_guard.py", "ci/patch_native_x_hulk_driver_compile_serial.py"),
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
    # The Windows release builder has its OWN late post-patch source checks.
    # Keep their exact source-call ABI synchronized with the real NativeX
    # patch, or CI wastes ~15 minutes compiling SDK before always failing.
    dma_call = "!CapturePacketDmaIndices(tracker, base, c, packet_surfaces != nullptr))"
    if dma_call not in BUILDER.read_text(encoding="utf-8"):
        raise RuntimeError("Windows builder has stale NativeX DMA capture ABI gate")
    from patch_native_x_hulk_packet_dma_snapshot import CALL_NEW
    if dma_call not in CALL_NEW:
        raise RuntimeError("NativeX DMA patch no longer emits builder-required signature")
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
        # Build 58 source evidence: 150k+ PM4 packets had *both* write
        # masks zero. The early gate is a no-op for GPU pixels and must
        # precede the expensive 5-slot XDK history lookup. True misses
        # remain fail-closed and counted separately.
        hulk_packet_at = renderer.find("bool HulkPacketSurfaces(")
        hulk_packet_end = renderer.find("void CaptureDraw(", hulk_packet_at)
        if hulk_packet_at < 0 or hulk_packet_end <= hulk_packet_at:
            missing.append("Hulk packet GPU surface capture function missing")
        else:
            surface_fn = renderer[hulk_packet_at:hulk_packet_end]
            early = surface_fn.find("HULK/PM4-NO-WRITE-EARLY")
            lookup = surface_fn.find("for (uint32_t slot = 0; slot < 5; ++slot)")
            if early < 0 or lookup < 0 or early >= lookup:
                missing.append("Hulk unwritable packet early no-op not before surface lookup")
            if "HULK/PM4-ACTUAL-MISS" not in surface_fn or "if (unresolved)" not in surface_fn:
                missing.append("Hulk actual color/depth attachment failures not isolated")

        texture=(source/"src/guest_textures.cpp").read_text(encoding="utf-8")
        if "HULK/TEXTURE-RESOURCE-GUARD" not in texture:
            missing.append("Hulk Vulkan texture descriptor guard")
        # Native host-resolution chain: the 640x360 guest scene and its
        # 640x360 resolved fetch may both represent 2560x1440 host pixels.
        # Check the generated actual C++, not only the synthetic patch fixture.
        resolved_cpp=(source/"src/targets.cpp").read_text(encoding="utf-8")
        if ("HULK/SHARED-RESOLVE-WRITE" not in resolved_cpp or
            "HulkSharedLatestWrite(target)" not in resolved_cpp):
            missing.append("Physical image shared write epoch missing on Resolve")
        resolved_h=(source/"src/targets.h").read_text(encoding="utf-8")
        for filename, content in (
            ("src/renderer.cpp", renderer),
            ("src/targets.cpp", resolved_cpp),
            ("src/targets.h", resolved_h),
        ):
            if "HULK/TRUE-RESOLVE-SCALE" not in content:
                missing.append("NativeX full-resolution scene Resolve: "+filename)
        if "g_resolved.SetHulkSampleGrid(" not in renderer:
            missing.append("resolved host texture initialization")
        if "width == hulk_half_width_ && height == hulk_half_height_" not in resolved_cpp:
            missing.append("per-dimension host Resolve upscaling")
        if "HULK/RESOLVE-RESOURCE-GUARD" not in resolved_cpp:
            missing.append("Hulk resolved Vulkan texture descriptor guard")
        state=(source/"src/state.cpp").read_text(encoding="utf-8")
        if ("HULK/PM4-REGISTER-SNAPSHOT" not in state or
            "hulk_restore_registers()" not in state):
            missing.append("PM4 registers must be frozen from the validated first scan")
        if ("HULK/PM4-PRESCAN-SHADER-SNAPSHOT" not in state or
            "hulk_prescan_words.data() + original.ucode_offset" not in state or
            "hulk_shader_replay_ok" not in state):
            missing.append("PM4 shader snapshot must survive validated gap replay")
        if ("HULK/PM4-GUEST-MEMORY-BOUNDS" not in state or
            "HULK/PM4-SHADER-SOURCE-BOUNDS" not in state or
            "uint64_t(address) + uint64_t(dwords) * 4ull" not in state):
            missing.append("NativeX physical guest shader/ALU source bounds")
        for name, body in (
            ("HULK/PM4-PAYLOAD",state),
            ("HULK/FETCH-WRITER",state),
            ("HULK/PM4-DMA-SNAPSHOT",renderer),
            ("HULK/FETCH-ORIGIN",draw),
            ("HULK/DMA-ORIGIN",draw),
            ("HULK/CPU-INDEX-SNAPSHOT",draw),
            ("processor.ProcessCapturedIndices(result, bytes.data(), base, size)",draw),
        ):
            if name not in body:missing.append(name)
        if sha == CANDIDATE_SHA:
            targets=(source/"src/targets.cpp").read_text(encoding="utf-8")
            if "native ? kNativeScale : scale_" not in targets or "&& !native &&" not in targets:
                missing.append("D127 native-scale target preserved by Hulk MSAA grid")
            if "source->scale.x != target->scale.x" not in renderer:
                missing.append("D127 scaled resolves preserved")
        if "HULK/SCENE-PIPELINE-WAIT" not in renderer or \
                "steady && !hulk_wait_scene" not in renderer:
            missing.append("Hulk bounded main-scene pipeline/shader wait")
        header_draw=(source/"src/draw.h").read_text(encoding="utf-8")
        if (HULK_SCENE_WAIT_CPP not in draw or
            "hulk_scene_wait_frame_" not in header_draw or
            "steady && !hulk_wait_scene, hulk_wait_scene);" not in renderer):
            missing.append("Hulk scene shader/pipeline waits are not frame-budgeted")
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
            ("HULK/PM4-DMA-INDEX-ENDIAN", renderer),
            ("HULK/NATIVEX-DRAW-FETCH-GUARD", draw),
            ("HULK/NATIVEX-VERTEX-FETCH-GUARD", draw),
            ("HULK/PM4-DMA-NO-LIVE-FALLBACK", draw),
            ("HULK/PM4-PERSISTENT-DMA", renderer),
            ("HULK/PM4-HISTORY-LIVE", renderer),
            ("HULK/PM4-RT0-1X-REBASE", renderer),
            ("HULK/PM4-DEPTH-REBASE-4X", renderer),
            ("HULK/PS-PROGRAM-ORIGIN", renderer),
            ("HULK/PS-RECENT-LOAD", state),
            ("HULK/PM4-FIRST-FAILURE", state),
            ("last_load_packet=", renderer),
            ("HULK/PM4-REGISTER-ORDER", state),
            ("HULK/PM4-SHADER-BOUNDS", state),
        ):
            if marker not in contents:
                missing.append("missing critical integrated C++: " + marker)
        # Rejected DMA Draws must NOT be queued as Command::Type::kDraw:
        # NativeX Capture::~Capture() publishes even after an early return.
        # Real XDK DrawIndexedVertices can inherit a valid initiator,
        # while synthetic PM4 draws must have their own packet-local one.
        # The previous hard-coded three-argument anchor incorrectly rejected
        # the source fix from #517cc5a before any C++ compilation.
        rejected = renderer.find(
            "!CapturePacketDmaIndices(tracker, base, c, packet_surfaces != nullptr))")
        if "HULK/XDK-PERSISTENT-INITIATOR" not in renderer:
            missing.append("real XDK persistent draw initiator protection missing")
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
