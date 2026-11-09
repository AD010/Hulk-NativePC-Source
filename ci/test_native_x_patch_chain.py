#!/usr/bin/env python3
"""NativeX integration gate: replay the COMPLETE actual Windows patch chain.

This tests the exact upstream pinned source before private XEX/codegen and the
long 800-object Windows compile. No game binaries, private data or public
build artifacts are involved.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
BUILDER=ROOT/"native_port"/"BUILD_WINDOWS.ps1"
SHA="bf965e952c98e050753e91d08f32685a9f754b12"
EXPERIMENTS=[
 "ci/test_patch_native_x_hulk_packet_first.py",
 "ci/patch_native_x_hulk_packet_first.py",
 "ci/test_patch_native_x_hulk_draw_write_ownership.py",
 "ci/patch_native_x_hulk_draw_write_ownership.py",
]

def commands():
    names=[]
    for line in BUILDER.read_text(encoding="utf-8").splitlines():
        if "Invoke-Checked 'python'" not in line:continue
        match=re.search(r"'(ci/(?:test_)?patch_native_x_hulk_[a-z_]+\.py)'",line)
        if match:
            names.append(match.group(1))
    # Append unshipped experiments in their real production order; never
    # duplicate a patch that has already been enabled in BUILD_WINDOWS.ps1.
    for name in EXPERIMENTS:
        if name not in names: names.append(name)
    if len(names)<25 or len(names)!=len(set(names)):
        raise RuntimeError(f"NativeX patch list incomplete or duplicated: {len(names)}")
    return names

def checked(args,cwd=ROOT):
    result=subprocess.run(args,cwd=cwd,stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT,text=True,errors="replace")
    if result.returncode:
        print(result.stdout[-7000:],file=sys.stderr)
        raise RuntimeError(f"Source-only NativeX preflight failed: {Path(args[1]).name}")
    return result.stdout

def run():
    scripts=commands()
    with tempfile.TemporaryDirectory(prefix="hulk-nxr-patch-") as directory:
        source=Path(directory)/"NativeXRenderer"
        checked(["git","clone","--no-checkout","--depth","1",
                 "https://github.com/ElGuslml/NativeXRenderer.git",str(source)])
        checked(["git","-C",str(source),"fetch","--depth","1","origin",SHA])
        checked(["git","-C",str(source),"checkout","--detach",SHA])
        for name in scripts:
            file=ROOT/name
            command=[sys.executable,str(file)]
            if not name.startswith("ci/test_"): command.append(str(source))
            checked(command)
            print("[NativeX/preflight] OK",name,flush=True)
        renderer=(source/"src/renderer.cpp").read_text(encoding="utf-8")
        draw=(source/"src/draw.cpp").read_text(encoding="utf-8")
        needed=["HULK/PM4-REGISTER-ONLY","const bool submitted = g_draw->Draw(",
                "HULK/PM4-SURFACE","HULK/MSAA-GRID"]
        missing=[mark for mark in needed if mark not in renderer]
        if "bool DrawPath::Draw(" not in draw:missing.append("bool DrawPath::Draw(")
        if missing:raise RuntimeError(f"NativeX patch chain markers absent: {missing}")
        print(f"[NativeX/preflight] All {len(scripts)} real pinned-source patches pass")
if __name__=="__main__":run()
