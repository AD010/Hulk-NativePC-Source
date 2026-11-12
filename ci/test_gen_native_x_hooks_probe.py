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
    assert "gpu_probe::Hit(index, 0x##addr, ctx)" in cpp
    assert "ctx.r3.u32, ctx.r4.u32, ctx.r5.u32" not in cpp
    assert "__imp__sub_##addr(ctx,base)" in cpp
    assert "case 0x8289D730u: return true;" in cpp
    assert "case 0x8289D888u: return true;" in cpp
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
print("GPU strong-hook generator synthetic and full Hulk profile tests: OK")
