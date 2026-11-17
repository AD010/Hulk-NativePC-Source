#!/usr/bin/env python3
"""Independent EoR XDK caller evidence: Ghidra XEX vs ReXGlue vs runtime.

This crosscheck never equates an XDK call with an original engine Draw,
shader, material, world matrix, camera or GPU register. Both codegen and
Ghidra must agree on the same direct-bl instruction at guest LR-4,
and runtime must have observed that exact LR in the pinned USA build.
No proprietary instruction, decompiled source or original assets emitted.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re

from hulk_engine_render_callgraph import USA_XEX_SHA256

HEX = re.compile(r"^0x[0-9A-F]{8}$")
XDK_CALLS = {
    "0x8288DF30": "DrawVertices",
    "0x8288E318": "DrawIndexedVertices",
    "0x828897F8": "Resolve",
    "0x8287F2C0": "SetRenderTarget",
    "0x8287F628": "SetDepthStencilSurface",
    "0x8288A900": "BeginTiling",
    "0x8288AD98": "EndTiling",
    "0x82888D68": "Clear",
    "0x82888C40": "ClearF",
    "0x82880778": "Present",
}


def check_hex(value: object) -> str:
    if not isinstance(value, str) or not HEX.fullmatch(value):
        raise ValueError("Invalid guest PPC address")
    addr = int(value[2:], 16)
    if not 0x82000000 <= addr < 0x84000000:
        raise ValueError("Ghidra address not in original USA XEX space")
    return value


def compare(ghidra: dict, observed: dict) -> dict:
    if (ghidra.get("schema") != "hulk_ghidra_usa_xdk_direct_v1"
            or observed.get("schema") !=
            "hulk_usa_runtime_to_static_xdk_callsite_v1"
            or not ghidra.get("powerpc_import_verified")
            or ghidra.get("engine_draw_abi_verified") is not False
            or ghidra.get("source_xex_sha256") != USA_XEX_SHA256
            or observed.get("source_xex_sha256") != USA_XEX_SHA256):
        raise ValueError("Ghidra and runtime use different or non-USA XEX")

    edges = ghidra.get("edges")
    matched = observed.get("matched")
    if (not isinstance(edges, list) or len(edges) > 100000 or
            not isinstance(matched, list) or len(matched) > 256):
        raise ValueError("Unbounded or missing PPC caller evidence")

    static = set()
    for edge in edges:
        if not isinstance(edge, dict):
            raise ValueError("Malformed Ghidra original PPC edge")
        xdk = check_hex(edge.get("xdk_guest_address"))
        site = check_hex(edge.get("static_direct_bl_site"))
        lr = check_hex(edge.get("observed_guest_lr"))
        caller = check_hex(edge.get("source_defined_guest_caller"))
        if (XDK_CALLS.get(xdk) != edge.get("verified_xdk_method") or
                edge.get("direct_bl") is not True or
                edge.get("engine_draw_abi_verified") is not False or
                int(lr, 16) != int(site, 16) + 4 or
                int(caller, 16) > int(site, 16)):
            raise ValueError("Unverifiable Ghidra original XDK callsite")
        identity = (xdk, site, caller)
        if identity in static:
            raise ValueError("Repeated Ghidra callsite must be unique")
        static.add(identity)

    confirmed = []
    unresolved = []
    for row in matched:
        if not isinstance(row, dict):
            raise ValueError("Malformed original runtime/callgraph row")
        xdk = check_hex(row.get("xdk_guest_address"))
        site = check_hex(row.get("static_direct_bl_site"))
        lr = check_hex(row.get("observed_guest_lr"))
        callers = row.get("source_defined_guest_callers")
        if (XDK_CALLS.get(xdk) != row.get("verified_xdk_method") or
                not isinstance(callers, list) or not callers or
                len(callers) > 32 or
                row.get("verified_direct_bl_link") is not True or
                row.get("verified_engine_draw_abi") is not False or
                int(lr, 16) != int(site, 16) + 4):
            raise ValueError("Invalid or already promoted original engine ABI")
        callers = [check_hex(caller) for caller in callers]
        agreed = sorted(set(callers).intersection(
            caller for candidate_xdk, candidate_site, caller in static
            if candidate_xdk == xdk and candidate_site == site))
        item = {
            "xdk_guest_address": xdk,
            "method": XDK_CALLS[xdk],
            "ppc_call_instruction": site,
            "runtime_lr": lr,
            "ghidra_and_codegen_callers": agreed,
            "source_bl_and_runtime_lr_triple_verified": bool(agreed),
            "engine_draw_abi_verified": False,
            "material_or_camera_verified": False,
        }
        (confirmed if agreed else unresolved).append(item)
    return {
        "schema": "hulk_usa_triple_xdk_witness_v1",
        "source_xex_sha256": USA_XEX_SHA256,
        "triple_witnessed_direct_bl_count": len(confirmed),
        "runtime_verified_but_ghidra_unresolved_count": len(unresolved),
        "triple_witnesses": confirmed,
        "unresolved": unresolved,
        "native_engine_draw_bridge_ready": False,
        "notes": "Direct PPC callsite proof only; no mesh/material/VS/PS/camera ABI inferred",
    }


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--ghidra", type=Path, required=True)
    p.add_argument("--runtime", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    for input_file in (args.ghidra, args.runtime):
        if not input_file.is_file() or input_file.stat().st_size > 24*1024*1024:
            raise ValueError("Missing or unbounded original PPC evidence")
    result = compare(json.loads(args.ghidra.read_text(encoding="utf-8")),
                     json.loads(args.runtime.read_text(encoding="utf-8")))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n",
                           encoding="utf-8")
    print("[HULK/EOR-GHIDRA] triple witnessed direct XDK calls:",
          result["triple_witnessed_direct_bl_count"],
          "Ghidra unresolved:",
          result["runtime_verified_but_ghidra_unresolved_count"])


if __name__ == "__main__":
    main()
