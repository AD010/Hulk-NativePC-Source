"""Adds the branch targets codegen could not place to missing_funcs.toml.

Codegen fails with lines such as
    0x82120140 from 0x824B4B2C: b 0x82120140 from 0x824B4B2C - target not in any function
Each such target is a function the analysis did not find on its own. This script reads a codegen
log, adds every new target to missing_funcs.toml as a function start (codegen finds its end) and
keeps the entries already there. Run codegen again afterwards; repeat until it passes.

A game log works too: the game stops with
    [FATAL] Call to invalid or unregistered function at guest address 0x82634F78
for a function reached through a pointer that tools/find_missing_funcs.py did not find. That address is added the
same way.

Usage: python tools/missing_funcs.py runs/codegen.log   (or a game log)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "missing_funcs.toml"
HEADER = """# Functions the analysis does not find on its own: branch targets that land outside every
# discovered function (codegen "target not in any function"), and functions the game called that codegen never
# made ("unregistered function"). Made by tools/missing_funcs.py.
[functions]
"""


def main():
    log = Path(sys.argv[1]).read_text(errors="replace")
    found = set(re.findall(r"^\s+0x([0-9A-F]{8}) from 0x[0-9A-F]{8}: .* target not in any function", log, re.M))
    found |= set(re.findall(r"unregistered function at guest address 0x([0-9A-F]{8})", log))
    # ReXGlue can fail during C++ emission after validation with a previously
    # unclassified branch. Capture these candidates for another analysis pass.
    found |= set(re.findall(r"Unresolved conditional branch to 0x([0-9A-F]{8}) from 0x[0-9A-F]{8}", log))
    # Direct PPC 'b' targets within automatically inferred function spans were
    # previously ignored, causing codegen pass 9 to report "0 new" forever.
    found |= set(re.findall(r"Unresolved (?:b target|function) 0x([0-9A-F]{8}) from 0x[0-9A-F]{8}", log))
    known = set()
    if OUT.exists():
        known = set(re.findall(r"^0x([0-9A-F]{8}) = ", OUT.read_text(), re.M))
    new = found - known
    lines = [f"0x{a} = {{}}\n" for a in sorted(known | found)]
    OUT.write_text(HEADER + "".join(lines), newline="\n")
    print(f"{len(new)} new, {len(known | found)} in all")


if __name__ == "__main__":
    main()
