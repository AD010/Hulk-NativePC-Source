#!/usr/bin/env python3
"""Read-only USA Hulk EoR input/camera/menu callgraph candidates.

This identifies *direct PPC callers*, not verified camera offsets or menu hooks.
Use after AOT codegen on the private runner. Never export private game bytes.
"""
from __future__ import annotations

INPUT_POLL_ANCHOR = 0x828AA818  # USA sub_828AA818 -> XamInputGetState


def analyze_input_context(reverse: dict[int, list[tuple[int, int]]],
                          render_report: dict, *,
                          max_depth: int = 4, limit: int = 120) -> dict:
    """Rank witnessed input callers, retaining exact direct PPC bl sites."""
    if not (1 <= max_depth <= 5 and 1 <= limit <= 500):
        raise ValueError("Invalid input callgraph report limits")
    group_by_function: dict[int, list[str]] = {}
    for row in render_report.get("candidate_ancestors", []):
        if "guest_function" in row:
            addr = int(row["guest_function"], 16)
            group_by_function[addr] = sorted(set(row.get("hook_groups", [])))

    known = {INPUT_POLL_ANCHOR}
    frontier: dict[int, list[dict]] = {INPUT_POLL_ANCHOR: []}
    candidates: dict[int, dict] = {}
    breadth: dict[str, int] = {}
    for depth in range(1, max_depth + 1):
        next_frontier: dict[int, list[dict]] = {}
        for callee in sorted(frontier):
            for caller, site in sorted(set(reverse.get(callee, ()))):
                if caller in known or caller == callee:
                    continue
                if not (0x82000000 <= caller < 0x84000000):
                    continue
                entry = {
                    "caller": f"0x{caller:08X}",
                    "call_site": f"0x{site:08X}",
                    "callee": f"0x{callee:08X}",
                    "op": "bl",
                }
                chain = [entry] + frontier[callee]
                previous = next_frontier.get(caller)
                if previous is None or tuple(x["call_site"] for x in chain) < tuple(
                        x["call_site"] for x in previous):
                    next_frontier[caller] = chain
        if not next_frontier:
            break
        # Mark visited after the full BFS layer to choose a stable shortest
        # witnessed path when two sources lead to the same function.
        breadth[str(depth)] = len(next_frontier)
        for caller, chain in sorted(next_frontier.items()):
            candidates[caller] = {
                "guest_function": f"0x{caller:08X}",
                "distance_to_xinput_read": depth,
                "direct_input_poll": depth == 1,
                "render_ancestor_groups": group_by_function.get(caller, []),
                "evidence_chain": chain,
                "validated_camera_layout": False,
                "validated_eor_menu_hook": False,
            }
        known.update(next_frontier)
        frontier = next_frontier
    ranked = sorted(candidates.values(), key=lambda row: (
        -len(row["render_ancestor_groups"]),
        row["distance_to_xinput_read"],
        int(row["guest_function"], 16)))
    return {
        "schema": "hulk_usa_eor_input_callgraph_v1",
        "input_wrapper_guest_address": f"0x{INPUT_POLL_ANCHOR:08X}",
        "input_wrapper_role": "original_game_XInputGetState_wrapper_from_input_hook_source",
        "analyzed_direct_bl_only": True,
        "depth": max_depth,
        "source": "SHA-verified USA XEX -> ReXGlue generated AOT guest callsites",
        "candidate_count": len(candidates),
        "ancestors_per_distance": breadth,
        "direct_poll_callers": sorted(
            (row for row in candidates.values() if row["direct_input_poll"]),
            key=lambda row: int(row["guest_function"], 16))[:32],
        "candidate_eor_update_or_menu_callers": ranked[:limit],
        "limitations": [
            "Candidates are direct PPC bl callers only; virtual bctrl dispatch is not resolved",
            "Shared render/input ancestry is only a search lead, not camera or menu identity",
            "No guessed class layouts, object offsets, function ABI, keyboard API or guest hooks",
            "Call sites and guest function start addresses require runtime validation",
            "Original XEX and save files are not written to this report",
        ],
    }
