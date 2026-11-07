#!/usr/bin/env python3
"""Reuse the six known cross-function 'b' targets from Hulk's failed 810-function run.

Every target below came from a real ReXGlue PPC branch-emission error, not a
guessed XDK/renderer function. The preceding pointer-scanned span must be
shortened so that the branch target can be recompiled as its own entry point.

This is idempotent and preserves every pre-existing cached function entry.
"""
from __future__ import annotations
import pathlib
import re
import sys
import csv

# ReXGlue run 42112949583, emitted "Unresolved b target ... from ...".
HULK_TAIL_TARGETS = (
    0x82124EC0,
    0x8214B950,
    0x82151CE0,
    0x82164CC0,
    0x8216ED60,
    0x82186480,
)

FUNCTION = re.compile(r"(?m)^0x([0-9a-fA-F]{8})\s*=\s*\{\s*\}\s*$")
RANGE = re.compile(
    r"(?m)^(?P<begin>0x(?P<start>[0-9a-fA-F]{8})\s*=\s*\{\s*end\s*=\s*0x)"
    r"(?P<end>[0-9a-fA-F]{8})"
    r"(?P<suffix>\s*\}\s*#\s*(?:pointer scan|data ptr @0x[0-9a-fA-F]+|lis/addi @0x[0-9a-fA-F]+)[^\r\n]*)$"
)

def required_hook_targets(root: pathlib.Path) -> tuple[int, ...]:
    """Only seed real guest entrypoints documented in Hulk's original recomp map.

    This makes the hook call-through symbols (__imp__sub_XXXXXXXX) exist in
    generated PPC C++, instead of adding fake native stubs or skipping the game.
    """
    mapping_path = root / "analysis/guest_host_mapping.csv"
    addresses_path = root / "native_hook_addresses.txt"
    input_path = root / "src/launcher/input_hook.cpp"
    with mapping_path.open(encoding="utf-8-sig", newline="") as fh:
        mapped = {int(row["guest_address"], 16) for row in csv.DictReader(fh)}
    requested: set[int] = set()
    for raw in addresses_path.read_text(encoding="utf-8-sig").splitlines():
        part = raw.split("#", 1)[0].strip().removeprefix("0x")
        if not part:
            continue
        if not re.fullmatch(r"[0-9a-fA-F]{8}", part):
            raise ValueError(f"Invalid renderer hook address: {raw!r}")
        requested.add(int(part, 16))
    input_source = input_path.read_text(encoding="utf-8-sig")
    input_hooks = re.findall(r"\b(?:REX_HOOK_RAW|HULK_LOCKED_INPUT)\s*\(\s*sub_([0-9a-fA-F]{8})\s*\)", input_source)
    if not input_hooks:
        raise ValueError("No input_hook.cpp guest entrypoints discovered; refusing silent link failure")
    for hit in input_hooks:
        requested.add(int(hit, 16))
    unknown = sorted(requested - mapped)
    if unknown:
        raise ValueError("Hooks not in verified guest map: " +
                         ", ".join(f"0x{x:08X}" for x in unknown))
    return tuple(sorted(requested))


def seed(missing: str, indirect: str, hook_targets: tuple[int, ...] = ()) -> tuple[str, str, list[int]]:
    found = {int(x, 16) for x in FUNCTION.findall(missing)}
    spans = list(RANGE.finditer(indirect))
    cuts: dict[int, int] = {}
    additions: list[int] = []
    for target in HULK_TAIL_TARGETS:
        enclosing = [m for m in spans if int(m["start"], 16) < target < int(m["end"], 16)]
        if len(enclosing) > 1:
            raise ValueError(f"0x{target:08X}: ambiguous inferred span")
        if enclosing:
            m = enclosing[0]
            start = int(m["start"], 16)
            cuts[start] = min(target, cuts.get(start, target))
        elif target not in found:
            # A cached build could already have replaced a span with a
            # standalone entry. A NEW target with no owner is ungrounded.
            raise ValueError(f"0x{target:08X}: no validated inferred span")
        if target not in found:
            additions.append(target)
    # Old mapping has these as real PPC functions. If they are not called
    # directly by the codegen scanner, they otherwise never get emitted, and
    # lld-link fails on the hook's __imp__sub_XXXXXXXX call-through.
    for target in hook_targets:
        owners = [m for m in spans if int(m["start"], 16) < target < int(m["end"], 16)]
        if owners:
            raise ValueError(f"0x{target:08X}: hook inside inferred function span; manual validation needed")
        if target not in found and target not in additions:
            additions.append(target)

    def update(m: re.Match[str]) -> str:
        start = int(m["start"], 16)
        if start not in cuts:
            return m[0]
        return m["begin"] + f"{cuts[start]:08X}" + m["suffix"]
    revised = RANGE.sub(update, indirect)
    if additions:
        revised_missing = missing.rstrip("\r\n") + "\n" + "".join(
            f"0x{addr:08X} = {{}}\n" for addr in sorted(additions))
    else:
        revised_missing = missing
    return revised_missing, revised, additions

def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: seed_hulk_ppc_tails.py missing_funcs.toml indirect_funcs.toml",
              file=sys.stderr)
        return 2
    missing_path, indirect_path = map(pathlib.Path, sys.argv[1:])
    try:
        original_missing = missing_path.read_text(encoding="utf-8-sig")
        original_indirect = indirect_path.read_text(encoding="utf-8-sig")
        hooks = required_hook_targets(missing_path.parent)
        updated_missing, updated_indirect, new = seed(original_missing, original_indirect, hooks)
        if new:
            missing_path.write_text(updated_missing, encoding="utf-8", newline="\n")
        if updated_indirect != original_indirect:
            indirect_path.write_text(updated_indirect, encoding="utf-8", newline="\n")
    except (ValueError, OSError) as exc:
        print("Cannot safely recover Hulk branch targets:", exc, file=sys.stderr)
        return 1
    count = len(FUNCTION.findall(updated_missing))
    print(f"Prepared {len(HULK_TAIL_TARGETS)} known PPC branch entries and "
          f"{len(hooks)} verified guest hook targets "
          f"({len(new)} newly added, {count} function starts in manifest).")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
