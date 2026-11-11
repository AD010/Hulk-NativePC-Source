#!/usr/bin/env python3
"""Locate PPC functions changed by the authenticated EU Hulk TU1.

Input source roots are ReXGlue codegen output from the SAME EU XEX, once
without and once with the verified sibling default.xexp. Never substitute
EU PPC addresses into the USA native port. The report contains no game
instructions or binary bytes, only hashes, counts and function addresses.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re

from audit_hulk_tu1 import xex_rsa_digest, xexp_source, digest

EU_XEX_SHA256 = "ede8b62b5d4d0357ace2e08caf06a6bd702783d1a9b036bbf405124bd84f3351"
EU_TU1_XEXP_SHA256 = "f0a34fdf59112ef144782da60c64cc56fa7ee4799e95d91992f4d0da298e5aae"
USA_XEX_SHA256 = "346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0"
EU_TU1_SOURCE_RSA_SHA1 = "8ea1670d19d66c322f50a4d1a6409466a86abada"
FUNC = re.compile(r"^DEFINE_REX_FUNC\(sub_([0-9A-Fa-f]{8})\)")
LABEL = re.compile(r"^\s*loc_([0-9A-Fa-f]{8}):")
# ReXGlue emits a "// Block: 0xXXXXXXXX" annotation at *every* basic
# block, including fallthrough blocks without a loc_ label. The previous
# comparator incorrectly treated "Block:" as a PPC instruction and invented
# subsequent addresses, making most TU1 deltas untrustworthy.
BLOCK = re.compile(r"^\s*// Block: 0x([0-9A-Fa-f]{8})\s*$")
ASSEMBLY = re.compile(r"^\s*// (.+?)\s*$")
# Generated PPC disassembly mnemonics are lowercase; "Block:", "TODO:"
# and other codegen documentation comments are not PPC instructions.
PPC_OP = re.compile(r"^[a-z][a-z0-9_.+-]*(?:\s|$)")


def check_exact_eu_patch(base_xex: bytes, xexp: bytes) -> dict:
    """Reject a wrong region, wrong XEXP, changed files or wrong patch base."""
    eu_sha = hashlib.sha256(base_xex).hexdigest()
    delta_sha = digest(xexp)
    if eu_sha != EU_XEX_SHA256:
        raise ValueError("Not the audited original EU Hulk XEX")
    if delta_sha != EU_TU1_XEXP_SHA256:
        raise ValueError("Not the audited EU Hulk TU1 XEXP")
    source, source_version, target_version = xexp_source(xexp)
    if source.hex() != EU_TU1_SOURCE_RSA_SHA1 or xex_rsa_digest(base_xex) != source:
        raise ValueError("XEXP source RSA digest does not match the EU XEX")
    if (source_version, target_version) != (2, 0x102):
        raise ValueError("TU1 version mismatch")
    return {"eu_xex_sha256": eu_sha, "tu1_xexp_sha256": delta_sha,
            "source_rsa_sha1": source.hex(), "source_version": source_version,
            "target_version": target_version, "source_verified": True}


def check_usa_executable(source: bytes) -> str:
    """Pin the region identity of optional USA codegen input."""
    actual = hashlib.sha256(source).hexdigest()
    if actual != USA_XEX_SHA256:
        raise ValueError("Not the SHA-pinned USA Hulk XEX; do not correlate regions")
    return actual


def read_recompiled_functions(root: Path) -> tuple[dict[int, dict], int]:
    """Stream C++ comments carrying guest PPC mnemonics, no code is exported."""
    paths = sorted(root.glob("*_recomp.*.cpp"))
    if not paths:
        raise ValueError("No ReXGlue *_recomp.N.cpp files in " + str(root))
    found = {}
    current = None
    start = None
    instructions = []
    instruction_addresses = []
    addresses_seen: set[int] = set()
    files_with_functions = 0

    def finish() -> None:
        if current is None:
            return
        if current in found:
            raise ValueError(f"Duplicate PPC guest function {current:08X}")
        if not instructions:
            raise ValueError(f"No PPC assembly metadata for {current:08X}")
        checksum = hashlib.sha256()
        for addr, op in zip(instruction_addresses, instructions):
            checksum.update(addr.to_bytes(4, "big"))
            checksum.update(op.encode("utf-8"))
            checksum.update(b"\n")
        found[current] = {
            "sha256": checksum.hexdigest(),
            "count": len(instructions),
            "addresses": tuple(instruction_addresses),
            "instructions": tuple(instructions),
        }

    for path in paths:
        with path.open(encoding="utf-8", errors="replace") as stream:
            had_functions = False
            for line in stream:
                match = FUNC.match(line)
                if match:
                    finish()
                    current = int(match.group(1), 16)
                    start = current
                    instructions = []
                    instruction_addresses = []
                    addresses_seen = set()
                    had_functions = True
                    continue
                if current is None:
                    continue
                label = LABEL.match(line)
                if label:
                    start = int(label.group(1), 16)
                    continue
                block = BLOCK.match(line)
                if block:
                    start = int(block.group(1), 16)
                    continue
                assembly = ASSEMBLY.match(line)
                if not assembly:
                    continue
                mnemonic = assembly.group(1).strip()
                if not PPC_OP.match(mnemonic):
                    continue
                if start in addresses_seen:
                    raise ValueError(f"Duplicate guest PPC instruction address {start:08X} in {current:08X}")
                addresses_seen.add(start)
                instruction_addresses.append(start)
                instructions.append(mnemonic)
                start += 4
        if had_functions:
            files_with_functions += 1
    finish()
    return found, files_with_functions


def compare(base: dict[int, dict], patched: dict[int, dict]) -> dict:
    if not base or not patched:
        raise ValueError("Cannot compare empty guest PPC function sets")
    before, after = set(base), set(patched)
    same = []
    altered = []
    for addr in sorted(before & after):
        b, p = base[addr], patched[addr]
        if b["sha256"] == p["sha256"]:
            same.append(addr)
            continue
        old_ops = dict(zip(b["addresses"], b["instructions"]))
        new_ops = dict(zip(p["addresses"], p["instructions"]))
        at = sorted(k for k in old_ops.keys() | new_ops.keys()
                    if old_ops.get(k) != new_ops.get(k))
        changes = sum(1 for k in at
                      if k in old_ops and k in new_ops)
        only_added = sum(1 for k in at if k not in old_ops)
        only_removed = sum(1 for k in at if k not in new_ops)
        altered.append({
            "guest_function": f"0x{addr:08X}",
            "guest_first_changed": f"0x{at[0]:08X}" if at else None,
            "changed_instruction_sites": len(at),
            "instruction_replacements": changes,
            "guest_sites_only_in_update": only_added,
            "guest_sites_only_in_base": only_removed,
            "base_instruction_count": b["count"],
            "tu1_instruction_count": p["count"],
            "base_sha256": b["sha256"],
            "tu1_sha256": p["sha256"],
            "note": "PPC change detected; gameplay purpose NOT identified",
        })
    return {
        "unchanged_function_count": len(same),
        "changed_function_count": len(altered),
        "added_function_addresses": [f"0x{x:08X}" for x in sorted(after - before)],
        "removed_function_addresses": [f"0x{x:08X}" for x in sorted(before - after)],
        "changed_guest_functions": altered,
    }



def normalized_base_signature(function: dict) -> str:
    """Region-independent *exact* signature for a PPC function.

    Only eight-digit branch targets are masked. Other immediates, register
    operands and instruction order remain intact: a fuzzy match here would
    risk treating unrelated EU/USA functions as equivalent.
    """
    def canonical(inst: str) -> str:
        op, _, rest = inst.partition(" ")
        if op.lower().startswith("b"):
            rest = re.sub(r"(?<![\w])(?:0x)?[0-9a-fA-F]{8}(?![\w])",
                          "<TARGET>", rest)
        return op + (" " + rest if rest else "")
    return hashlib.sha256(
        "\n".join(canonical(inst) for inst in function["instructions"])
        .encode("utf-8")
    ).hexdigest()


def match_usa_functions(eu_base: dict, usa: dict, changed: list,
                        min_instructions: int = 24) -> list[dict]:
    """Find unique candidate USA functions from *unpatched* EU PPC.

    This is a conservative address-correlation report, NOT a TU1 behavior
    port or a reason to install a hook. Short or duplicate functions fail
    closed. EU TU1 itself is never compared directly to an unpatched USA
    binary, since that would hide an update-specific behavior change.
    """
    lookup: dict[str, list[int]] = defaultdict(list)
    for addr, function in usa.items():
        if function["count"] >= min_instructions:
            lookup[normalized_base_signature(function)].append(addr)

    result = []
    for changed_fn in changed:
        eu_address = int(changed_fn["guest_function"], 16)
        baseline = eu_base[eu_address]
        status = ("too_short" if baseline["count"] < min_instructions
                  else "no_unique_exact_match")
        address = None
        if baseline["count"] >= min_instructions:
            matches = lookup.get(normalized_base_signature(baseline), ())
            if len(matches) == 1:
                status = "unique_normalized_ppc_match"
                address = f"0x{matches[0]:08X}"
            elif len(matches) > 1:
                status = "ambiguous_identical_functions"
        result.append({
            "eu_guest_function": changed_fn["guest_function"],
            "usa_candidate": address,
            "status": status,
            "note": "Address correlation only; TU1 behavior and water role NOT verified",
        })
    return result


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--eu-xex", type=Path, required=True)
    p.add_argument("--xexp", type=Path, required=True)
    p.add_argument("--eu-base-generated", type=Path, required=True)
    p.add_argument("--eu-tu1-generated", type=Path, required=True)
    p.add_argument("--us-generated", type=Path,
                   help="Optional USA codegen output from the same ReXGlue revision")
    p.add_argument("--us-xex", type=Path,
                   help="Mandatory original SHA-pinned USA XEX when --us-generated is supplied")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if (args.us_generated is None) != (args.us_xex is None):
        p.error("--us-generated and --us-xex must be supplied together")
    provenance = check_exact_eu_patch(args.eu_xex.read_bytes(), args.xexp.read_bytes())
    old, old_files = read_recompiled_functions(args.eu_base_generated)
    new, new_files = read_recompiled_functions(args.eu_tu1_generated)
    # Require the same function address space. A wrong executable or a
    # completely different codegen run must not masquerade as a small TU1.
    overlap = len(set(old) & set(new))
    if overlap * 2 < min(len(old), len(new)):
        raise ValueError("EU TU1 codegen address spaces differ too much")
    analysis = compare(old, new)
    report = {
        "schema": "hulk_eu_tu1_guest_ppc_diff_v1",
        "verified_patch": provenance,
        "base_generated_files": old_files,
        "tu1_generated_files": new_files,
        "base_functions": len(old),
        "tu1_functions": len(new),
        "comparison": analysis,
        "us_code_patched": False,
        "water_fix_identified": False,
        "limitations": [
            "A changed function does not by itself identify a water-handling fix",
            "The EU update may change game resources as well as PPC code",
            "Use caller/callee evidence and game event state traces to locate the water transition",
            "NEVER port European PPC addresses or raw XEXP into the pinned USA codegen",
            "No game bytecode, assembly listing or assets are emitted by this tool",
        ],
    }
    if args.us_generated is not None:
        report["usa_xex_sha256"] = check_usa_executable(args.us_xex.read_bytes())
        us_functions, us_files = read_recompiled_functions(args.us_generated)
        report["usa_generated_files"] = us_files
        report["usa_function_count"] = len(us_functions)
        report["usa_candidates"] = match_usa_functions(
            old, us_functions, analysis["changed_guest_functions"])
        report["usa_mapping_limitation"] = (
            "Even unique exact PPC sequence correlation is only a candidate: "
            "verify graph, data references and game events before porting logic")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("[HULK/TU1/PPC] authenticated EU baseline and EU update compared: "
          f"{analysis['changed_function_count']} modified functions, "
          f"{len(analysis['added_function_addresses'])} added, "
          f"{len(analysis['removed_function_addresses'])} removed; "
          "water-fix role UNVERIFIED; USA code UNCHANGED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
