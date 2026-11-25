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
        "0x8264C798,0x1427F9EA0\n"
        "0x825F8A50,0x142B864A0\n"
        "0x825F84C0,0x1419FCDC0\n"
        "0x82642370,0x1404A3720\n"
        "0x8263DCD0,0x1408C14B0\n")
    addresses.write_text("8289D730\n8289D888\n8265B420\n"
                         "8266A0C0\n8264C798\n825F8A50\n"
                         "825F84C0\n82642370\n8263DCD0\n")
    subprocess.run([sys.executable,str(s),str(addresses),
                    str(mapping),str(generated),str(profile)],check=True)
    cpp = generated.read_text()
    assert "HULK_EOR_SHADER_TRACE_HOOK(8265B420)" in cpp
    assert "HULK_EOR_SHADER_TRACE_HOOK(8266A0C0)" in cpp
    assert "HULK_EOR_VM_STATE_TRACE_HOOK(8264C798)" in cpp
    assert "HULK_NATIVE_X_HOOK(8264C798" not in cpp
    assert "ObserveOriginalEorVmState_r(0x##addr,ctx,base)" in cpp
    assert "HULK_EOR_MATERIAL_TRACE_HOOK(825F8A50)" in cpp
    assert "HULK_NATIVE_X_HOOK(825F8A50" not in cpp
    assert "ObserveOriginalEorMaterialDescriptor(0x##addr,ctx,base)" in cpp
    assert "HULK_EOR_SHADER_PASS_TRACE_HOOK(825F84C0)" in cpp
    assert "HULK_NATIVE_X_HOOK(825F84C0" not in cpp
    assert "ObserveOriginalEorShaderPass(0x##addr,ctx,base)" in cpp
    assert "HULK_EOR_RENDER_ITEM_TRACE_HOOK(82642370)" in cpp
    assert "HULK_EOR_RENDER_ITEM_TRACE_HOOK(8263DCD0)" in cpp
    assert "HULK_NATIVE_X_HOOK(82642370" not in cpp
    assert "HULK_NATIVE_X_HOOK(8263DCD0" not in cpp
    assert "ScopedOriginalEorRenderItem source_render_item" in cpp
    assert cpp.index("ScopedOriginalEorRenderItem source_render_item") < (
        cpp.index("__imp__sub_##addr(ctx,base)",cpp.index(
            "#define HULK_EOR_RENDER_ITEM_TRACE_HOOK")))
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
# The two new source-pinned segment boundaries are validated against
# real generated DEFINE_REX_FUNC bodies in the actual Windows build.
# Do not fabricate historical host PE addresses for new functions.
segment_roles = {"82886428": "KickSegment", "82885FF8": "NewSegment"}
assert all(required.get(addr)==role for addr,role in segment_roles.items())
assert not (absent := (set(required)-set(registered)-set(segment_roles))), (
    "Profile guest functions missing from PPC host map: " +
    ", ".join(sorted(absent)))
assert registered["82888D68"] == 0x1410ECD70, "USA Clear PPC/AOT mapping drift"
assert registered["82888C40"] == 0x141FEC5A0, "USA ClearF PPC/AOT mapping drift"

with tempfile.TemporaryDirectory() as tmp:
    out = Path(tmp) / "hooks.cpp"
    subprocess.run([sys.executable, str(s), str(project / "native_hook_addresses.txt"),
        str(project / "analysis/guest_host_mapping.csv"), str(out),
        str(project / "hulk_native_x_profile.ini")], check=True)
    cpp = out.read_text()
    for address in ("82338A00","82338A18"):
        assert "HULK_EOR_CAMERA_GETTER_HOOK(" + address + ")" in cpp
    assert "HULK_EOR_HAVOK_CATCHUP_HOOK(82738B58)" in cpp
    assert "HULK_EOR_HAVOK_CATCHUP_HOOK(82611740)" not in cpp
    assert "HULK_NATIVE_X_HOOK(82738B58" not in cpp
    for address in segment_roles:
        assert "HULK_NATIVE_X_HOOK(" + address + "," in cpp
        assert "case 0x" + address + "u: return true;" in cpp
# Seed preparation and actual hook generation must agree about new USA
# entrypoints which did not exist in the old PE mapping. Failure here is
# cheap, whereas discovering this drift on the private runner wastes a build.
from seed_hulk_ppc_tails import PINNED_SOURCE_HOOKS, PINNED_USA_SHA256, required_hook_targets
from unittest.mock import patch
import hashlib
expected_hooks = {0x82338A00,0x82338A18,0x82885FF8,0x82886428,
                  0x82339C18,0x82339C90,0x82338AE8,0x82338B20,0x82738B58,
                  0x825BAAC8}
# Direct original widget routines must have codegen bodies but must not
# become XDK hooks. Seed them automatically from the menu dispatcher.
menu_calls = {0x825B8270,0x825B9528,0x825B9D98,0x8271D340,0x8271AA60}
expected = expected_hooks | menu_calls
assert PINNED_SOURCE_HOOKS == expected
assert {int(x,16) for x in compiled if x} - {int(x,16) for x in registered} <= expected_hooks
assert "HULK_EOR_PC_MENU_HOOK(825BAAC8)" in cpp
assert "HULK_NATIVE_X_HOOK(825BAAC8," not in cpp
for address in menu_calls:
    assert f"{address:08X}" not in compiled
    assert f"HULK_NATIVE_X_HOOK({address:08X}," not in cpp
for address in ("82339C18","82339C90","82338AE8","82338B20"):
    assert "HULK_EOR_CAMERA_SPEED_HOOK(" + address + ")" in cpp
    assert "HULK_NATIVE_X_HOOK(" + address not in cpp
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    (root/"analysis").mkdir()
    (root/"src/launcher").mkdir(parents=True)
    (root/"game").mkdir()
    (root/"analysis/guest_host_mapping.csv").write_text(
        "guest_address,native_entry\n0x8289D730,0x140001000\n")
    (root/"src/launcher/input_hook.cpp").write_text("REX_HOOK_RAW(sub_8289D730)")
    hooks = root/"native_hook_addresses.txt"
    hooks.write_text("\n".join(f"{a:08X}" for a in sorted(expected_hooks)))
    (root/"game/default.xex").write_bytes(b"synthetic fixture only")
    try:
        required_hook_targets(root)
    except ValueError as exc:
        assert "pinned original USA XEX" in str(exc)
    else:
        raise AssertionError("Incorrect USA XEX accepted")
    with patch.object(hashlib, "sha256") as digest:
        digest.return_value.hexdigest.return_value = PINNED_USA_SHA256
        assert set(required_hook_targets(root)) == expected | {0x8289D730}
        hooks.write_text("\n".join(f"{a:08X}" for a in sorted(expected_hooks - {0x825BAAC8})))
        assert set(required_hook_targets(root)) == (expected_hooks - {0x825BAAC8}) | {0x8289D730}
        hooks.write_text("\n".join(f"{a:08X}" for a in sorted(expected_hooks))+"\n82738B5C\n")
        try:
            required_hook_targets(root)
        except ValueError as exc:
            assert "0x82738B5C" in str(exc)
        else:
            raise AssertionError("Unknown PPC hook accepted")
print("GPU strong-hook generator synthetic and full Hulk profile tests: OK")
