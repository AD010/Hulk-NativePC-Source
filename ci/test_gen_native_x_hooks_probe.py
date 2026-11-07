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
    addresses.write_text("8289D730\n8289D888\n")
    subprocess.run([sys.executable,str(s),str(addresses),str(mapping),str(generated)],check=True)
    cpp = generated.read_text()
    assert "HULK_NATIVE_X_HOOK(8289D730,0)" in cpp
    assert "HULK_NATIVE_X_HOOK(8289D888,1)" in cpp
    assert "gpu_probe::Hit(index, 0x##addr" in cpp
    assert "__imp__sub_##addr(ctx,base)" in cpp
    addresses.write_text("8289D730\n8289D730\n")
    p = subprocess.run([sys.executable,str(s),str(addresses),str(mapping),str(generated)],
                       capture_output=True,text=True)
    assert p.returncode and "Duplicate hook" in p.stderr+p.stdout
print("GPU strong-hook generator synthetic tests: OK")
