#!/usr/bin/env python3
"""Verify genuine USA PPC XDK Draw ABI using private SHA-pinned AOT evidence.

Output contains only original function/branch addresses, never game
instructions. XDK ABI is NOT an EoR mesh/material or Pause UI dispatcher ABI.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
from zipfile import ZipFile

USA_XEX_SHA256 = "346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0"
PRIVATE_EVIDENCE_SHA256 = "bfa31980f79938619ea703fd51821a85970f183c3bd7f382ce836146b51834d1"
SPECS = {
    "DrawVertices": {
        "guest": 0x8288DF30, "copies": (31, 25, 21, 22),
        "roles": ("device", "primitive", "start_vertex", "vertex_count"),
        "calls": ((0x826762C0, 0x826762E4, "b"),
                  (0x82676400, 0x8267642C, "bl")),
    },
    "DrawIndexedVertices": {
        "guest": 0x8288E318, "copies": (31, 16, 15, 19, 17),
        "roles": ("device", "primitive", "base_vertex", "first_index",
                  "index_count"),
        "calls": ((0x82640578, 0x82640610, "bl"),
                  (0x82641618, 0x82641648, "bl"),
                  (0x8265B420, 0x8265B538, "bl"),
                  (0x826680B8, 0x82668180, "bl"),
                  (0x826688D0, 0x82668998, "bl"),
                  (0x82676288, 0x826762B8, "b")),
    },
}


def load_private(path: Path) -> dict:
    if path.suffix.lower() == ".zip":
        with ZipFile(path) as archive:
            if archive.namelist() != ["hulk_6534_ppc_evidence.json"]:
                raise ValueError("Unexpected private original AOT evidence archive")
            raw = archive.read("hulk_6534_ppc_evidence.json")
    else:
        raw = path.read_bytes()
    if len(raw) > 16 * 1024 * 1024:
        raise ValueError("Unbounded private original AOT evidence")
    if hashlib.sha256(raw).hexdigest() != PRIVATE_EVIDENCE_SHA256:
        raise ValueError("Evidence must match the exact private PPC report")
    return json.loads(raw)


def inspect(report: dict) -> dict:
    if report.get("schema") != "hulk_native_xdk_6534_evidence_v1" or (
        report.get("xex_sha256", "").lower() != USA_XEX_SHA256 or
        report.get("guest_function_definitions_scanned") != 98726
    ):
        raise ValueError("Not the original 98,726-function USA PPC corpus")
    routes = {}
    for name, spec in SPECS.items():
        address = "0x%08X" % spec["guest"]
        raw = report.get("candidates", {}).get(address)
        if not isinstance(raw, dict):
            raise ValueError("Missing original guest Draw: " + address)
        decoded = raw.get("first_ppc_instructions", [])[:20]
        moves = []
        for index, instruction in enumerate(decoded):
            match = re.fullmatch(r"mr r(\d+),r(\d+)",
                                 instruction.get("asm", ""))
            if match:
                moves.append((index, int(match[1]), int(match[2])))
        found = []
        for reg in range(3, 3 + len(spec["roles"])):
            matches = [(pos, dst) for pos, dst, src in moves if src == reg]
            if len(matches) != 1:
                raise ValueError("Guest PPC argument copy unverified: " + name)
            found.append(matches[0])
        if tuple(dst for _, dst in found) != spec["copies"] or (
            [pos for pos, _ in found] != sorted(pos for pos, _ in found)
        ) or not any(line.get("asm") == "ld r4,0(r31)" for line in decoded):
            raise ValueError("Original USA XDK PPC register ABI differs: " + name)
        callers = sorted((int(row["caller_guest"], 16),
                          int(row["site"], 16), row["kind"])
                         for row in raw.get("direct_callers", []))
        if callers != sorted(spec["calls"]):
            raise ValueError("Static original USA XDK Draw caller graph differs")
        branches = []
        for caller, site, kind in callers:
            if not (0x82000000 <= caller <= site < 0x84000000) or (
                site & 3
            ) or kind not in ("b", "bl"):
                raise ValueError("Invalid original guest direct branch")
            branches.append({
                "source_defined_caller_function": "0x%08X" % caller,
                "static_direct_branch_site": "0x%08X" % site,
                "powerpc_branch_kind": kind,
                # Tail branches preserve incoming LR; do not invent a
                # guest engine owner or original Draw/material ABI.
                "expected_xdk_lr": "0x%08X" % (site + 4) if kind == "bl"
                                   else None,
                "source_verified_direct_branch": True,
                "original_engine_mesh_material_abi_verified": False,
            })
        routes[name] = {
            "guest_xdk_function": address,
            "source_verified_original_ppc_argument_registers":
                ["r%d" % i for i in range(3, 3 + len(spec["roles"]))],
            "xdk_method_argument_roles": list(spec["roles"]),
            "register_copies_verified_against_pinned_usa_aot": True,
            "source_defined_direct_callers": branches,
            "original_engine_material_draw_handler_verified": False,
        }
    return {
        "schema": "hulk_usa_verified_xdk_boundary_callsite_witness_v1",
        "source_xex_sha256": USA_XEX_SHA256,
        "private_source_ppc_evidence_sha256": PRIVATE_EVIDENCE_SHA256,
        "private_source_artifact": "Hulk-XDK6534-Evidence-42406003655.zip",
        "generated_ppc_functions_scanned": 98726,
        "draw_xdk_abis": routes,
        "xdk_entry_abi_verified": True,
        "original_pause_settings_ppc_dispatcher_abi_verified": False,
        "original_eor_engine_model_mesh_material_shader_abi_verified": False,
        "full_eor_vulkan_frame_draws_verified": False,
        "no_original_guest_instructions_or_private_game_bytes": True,
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--private-evidence", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()
    result = inspect(load_private(args.private_evidence))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n",
                           encoding="utf-8")
    print("[HULK/USA-XDK] Original XDK PPC entry ABI and eight direct "
          "branches proven; EoR engine material and pause ABI unproven")


if __name__ == "__main__":
    main()
