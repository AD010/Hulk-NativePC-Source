#!/usr/bin/env python3
"""Generator unit test against synthetic guest mapping, without any game XEX."""
from pathlib import Path
import subprocess
import sys
import tempfile
s = Path(__file__).resolve().parents[1] / "native_port/tools/gen_native_x_hooks.py"
with tempfile.TemporaryDirectory() as d:
    root = Path(d)
    addresses = root / "addresses.txt"
    mapping = root / "mapping.csv"
    generated = root / "hooks.cpp"
    mapping.write_text("guest_address,native_entry\n0x8289D730,0x140001000\n0x8289D888,0x140002000\n")
    # The old build allowed a profile to name hooks that were never compiled,
    # then silently returned to Xenos despite native_x_renderer=true.
    profile = root / "hulk_native_x_profile.ini"
    profile.write_text("[hooks]\n0x8289D730=Clear\n0x8289D888=ClearF\n")
    addresses.write_text("8289D730\n8289D888\n")
    subprocess.run([sys.executable,str(s),str(addresses),str(mapping),str(generated),str(profile)],check=True)
    cpp = generated.read_text()
    assert "HULK_NATIVE_X_HOOK(8289D730,0)" in cpp
    assert "HULK_NATIVE_X_HOOK(8289D888,1)" in cpp
    # The default release wrapper does not even link the PPC ABI
    # profiler; the original NativeX Enter/Leave call-through is kept.
    assert "#if defined(HULK_NATIVE_GPU_HOOK_PROBE)" in cpp
    assert "#define HULK_GPU_PROBE_HIT(index, addr, ctx) ((void)0)" in cpp
    assert "HULK_GPU_PROBE_HIT(index, 0x##addr, ctx)" in cpp
    assert "CapturePpcDraw" in cpp
    assert "CapturePpcCommand" in cpp
    assert "CommandKind::SetRenderTarget" in cpp
    assert "CommandKind::ClearF" in cpp
    assert "CommandKind::Resolve" in cpp
    assert "CommandKind::Present" in cpp
    assert "CommandKind::Clear, " not in cpp # nested Clear -> ClearF once
    assert "ctx, base);" in cpp
    assert "EndPpcHook(type)" in cpp
    # NativeX detail::Leave queues kPresent to its independent Vulkan render
    # worker. An EoR mailbox published afterwards races with that worker,
    # which can reach its only Present before the captured USA frame exists.
    # Keep the unhooked fallback intact, and publish the original PPC frame
    # after the guest function returns but before NativeX queues Present.
    guest = cpp.index("::native_x_renderer::detail::Enter(type,ctx,base);")
    original = cpp.index("__imp__sub_##addr(ctx,base);", guest)
    publish = cpp.index("::hulk::eor::xdk::EndPpcHook(type);", guest)
    native_leave = cpp.index("::native_x_renderer::detail::Leave(type,ctx,base);", guest)
    assert guest < original < publish < native_leave
    assert cpp.count("::hulk::eor::xdk::EndPpcHook(type);") == 2
    assert cpp.count("::native_x_renderer::detail::Leave(type,ctx,base);") == 1
    assert "frame to the worker" in cpp
    assert "eor_xdk_hook_bridge.h" in cpp
    assert "gpu_probe::Hit(index, addr, ctx)" in cpp
    cmake = (Path(__file__).resolve().parents[1] / "native_port/CMakeLists.txt").read_text()
    assert "option(HULK_NATIVE_GPU_HOOK_PROBE" in cmake
    assert "HULK_NATIVE_GPU_HOOK_PROBE=1" in cmake
    assert "if(HULK_NATIVE_GPU_HOOK_PROBE)" in cmake
    assert "src/native_gpu_hook_probe.cpp" in cmake
    assert "ctx.r3.u32, ctx.r4.u32, ctx.r5.u32" not in cpp
    assert "__imp__sub_##addr(ctx,base)" in cpp
    assert "case 0x8289D730u: return true;" in cpp
    assert "case 0x8289D888u: return true;" in cpp
    # The actual USA PPC engine has a named EoR shader class vtable;
    # its method is NOT an XDK method and must never call NativeX Enter/
    # Leave. It only owns a read-only scope around the original call.
    mapping.write_text(
        "guest_address,native_entry\n"
        "0x8289D730,0x140001000\n"
        "0x8289D888,0x140002000\n"
        "0x8265B420,0x14025EDC0\n"
        "0x8266A0C0,0x142BE0180\n"
        "0x8264C798,0x1427F9EA0\n")
    addresses.write_text("8289D730\n8289D888\n8265B420\n"
                         "8266A0C0\n8264C798\n")
    subprocess.run([sys.executable,str(s),str(addresses),
                    str(mapping),str(generated),str(profile)],check=True)
    cpp = generated.read_text()
    assert "HULK_EOR_SHADER_TRACE_HOOK(8265B420)" in cpp
    assert "HULK_EOR_SHADER_TRACE_HOOK(8266A0C0)" in cpp
    assert "HULK_EOR_VM_STATE_TRACE_HOOK(8264C798)" in cpp
    assert "HULK_NATIVE_X_HOOK(8264C798" not in cpp
    assert "ObserveOriginalEorVmState_r(0x##addr,ctx,base)" in cpp
    assert "HULK_NATIVE_X_HOOK(8265B420" not in cpp
    assert "HULK_NATIVE_X_HOOK(8266A0C0" not in cpp
    engine = cpp[cpp.index("#define HULK_EOR_SHADER_TRACE_HOOK"): ]
    assert "ScopedOriginalEorShaderMethod" in engine
    assert engine.index("ScopedOriginalEorShaderMethod") < engine.index(
        "__imp__sub_##addr(ctx,base)")
    assert "native_x_renderer::detail::Enter" not in engine.split(
        "HULK_EOR_SHADER_TRACE_HOOK(8265B420)",1)[0]
    profile.write_text("[hooks]\n0x8289D730=Clear\n"
                       "0x8289D888=ClearF\n"
                       "0x8265B420=DrawIndexedVertices\n")
    blocked = subprocess.run(
        [sys.executable,str(s),str(addresses),str(mapping),
         str(generated),str(profile)],capture_output=True,text=True)
    assert blocked.returncode
    assert "incorrectly declared an XDK profile entry" in (
        blocked.stdout+blocked.stderr)
    profile.write_text("[hooks]\n0x8289D730=Clear\n0x8289D888=ClearF\n")

    # Regression of Hulk #68: a missing Clear/ClearF strong hook must fail
    # the source generation step, not discover the omission during gameplay.
    addresses.write_text("8289D730\n")
    p = subprocess.run([sys.executable,str(s),str(addresses),
                        str(mapping),str(generated),str(profile)],
                       capture_output=True,text=True)
    assert p.returncode and "Profile has uncompiled NativeX PPC hooks" in p.stderr+p.stdout
    assert "0x8289D888=ClearF" in p.stderr+p.stdout

    addresses.write_text("8289D730\n8289D730\n")
    p = subprocess.run([sys.executable,str(s),str(addresses),str(mapping),str(generated)],
                       capture_output=True,text=True)
    assert p.returncode and "Duplicate hook" in p.stderr+p.stdout
# Check the checked-in USA profile against the ACTUAL compiled allowlist.
from configparser import ConfigParser
project = Path(__file__).resolve().parents[1] / "native_port"
cfg = ConfigParser(interpolation=None)
assert cfg.read(project / "hulk_native_x_profile.ini", encoding="utf-8")
required = {key.removeprefix("0x").upper():method for key,method in cfg.items("hooks")}
compiled = {line.split("#",1)[0].strip().removeprefix("0x").upper()
            for line in (project / "native_hook_addresses.txt").read_text().splitlines()}
assert not (missing := (set(required) - compiled)), (
    "Missing compiled XDK method wrappers: " +
    ", ".join("0x"+key+"="+required[key] for key in sorted(missing)))
assert required.get("82888D68") == "Clear"
assert required.get("82888C40") == "ClearF"
# A checked-in address alone is not enough: the translator's guest->host
# map must contain the exact AOT entry the compiled XEX will call.
import csv
with (project / "analysis/guest_host_mapping.csv").open(encoding="utf-8-sig",
                                                       newline="") as stream:
    rows = list(csv.DictReader(stream))
registered = {r["guest_address"].removeprefix("0x").upper(): int(
                  r["native_entry"], 16) for r in rows}
assert not (absent := (set(required)-set(registered))), (
    "Profile guest functions missing from PPC host map: " +
    ", ".join(sorted(absent)))
assert registered["82888D68"] == 0x1410ECD70, "USA Clear PPC/AOT mapping drift"
assert registered["82888C40"] == 0x141FEC5A0, "USA ClearF PPC/AOT mapping drift"

print("GPU strong-hook generator synthetic and full Hulk profile tests: OK")
