#!/usr/bin/env python3
"""Match REAL Hulk USA runtime XDK return addresses to static PPC bl sites.

The read-only native XDK logger now aggregates each exact (XDK, LR) callsite.
This tool checks whether LR-4 is a witnessed PowerPC direct branch
instruction in the USA SHA-verified ReXGlue generated source. It does NOT
decode r3..r10 as EoR Draw, assume a camera/technique ABI, or change hooks.

Private inputs: actual USA default.xex, source generated from that exact
XEX, user's runtime log. Output: guest address-only JSON, no game bytes.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
from hulk_engine_render_callgraph import (
    USA_XEX_SHA256, read_hooks, scan_direct_calls, verify_us_source
)

LINE = re.compile(
    r'\[HULK/EOR-XDK-CALLSITE\]\s+XDK=0x([0-9A-Fa-f]{8})\s+'
    r'LR=0x([0-9A-Fa-f]{8})\s+kind=(\d+)\s+'
    r'draws=(\d+)\s+frames=(\d+)(?=\s|$)')
MAX_TRACE_BYTES = 128 * 1024 * 1024
MAX_CALLSITES = 256


def analyze(log: str, hooks: dict[int, str],
            reverse: dict[int, list[tuple[int, int]]]) -> dict:
    named = {v: k for k, v in hooks.items()}
    allowed = {named[n] for n in ('DrawVertices', 'DrawIndexedVertices')
               if n in named}
    if not allowed:
        raise ValueError("Missing verified Hulk USA original XDK draw hooks")
    # The new caller logger aggregates repeatedly, so retain only the
    # latest cumulative observation per precise XDK+LR, and reject
    # decreasing counts/replayed or inconsistent hook-method types.
    observed: dict[tuple[int, int], dict] = {}
    for line in log.splitlines():
        if '[HULK/EOR-XDK-CALLSITE]' not in line:
            continue
        # Never skip a damaged tagged record to match a later valid one.
        marker = '[HULK/EOR-XDK-CALLSITE]'
        if line.count(marker) != 1:
            raise ValueError("Malformed Hulk XDK caller observation")
        m = LINE.search(line)
        if m and m.start() != line.index(marker):
            raise ValueError("Malformed Hulk XDK caller observation")
        # A tagged observation must contain exactly one complete record;
        # a second record or a glued-on numeric suffix is not evidence.
        if m and ('[HULK/EOR-XDK-CALLSITE]' in line[m.end():] or
                  re.search(r'(?<!\w)(?:XDK|LR|kind|draws|frames)=', line[m.end():])):
            raise ValueError("Malformed Hulk XDK caller observation")
        if not m:
            raise ValueError("Malformed Hulk XDK caller observation")
        xdk, lr = int(m[1], 16), int(m[2], 16)
        kind, draws, frames = map(int, m.groups()[2:])
        if xdk not in allowed or (xdk == named.get('DrawVertices') and kind != 0) or (
                xdk == named.get('DrawIndexedVertices') and kind != 1):
            raise ValueError("Runtime Hulk XDK caller conflicts with verified USA profile")
        if not (0x82000000 <= lr < 0x84000000 and
                0 < draws <= 10**9 and 0 < frames <= 10**9 and frames <= draws):
            raise ValueError("Invalid guest caller or cumulative sample count")
        key = (xdk, lr)
        prev = observed.get(key)
        if prev and (draws < prev['draws'] or frames < prev['frames']):
            raise ValueError("Out-of-order or inconsistent XDK caller summary")
        if not prev or draws > prev['draws'] or frames > prev['frames']:
            observed[key] = {'xdk': xdk, 'lr': lr,
                             'kind': kind, 'draws': draws, 'frames': frames}
        if len(observed) > MAX_CALLSITES:
            raise ValueError("Unbounded original Hulk XDK caller trace")
    if not observed:
        raise ValueError("No real HULK/EOR-XDK-CALLSITE observations in runtime log")
    matched = []
    unmatched = []
    for (xdk, lr), row in sorted(observed.items()):
        # PPC bl sets LR to the address of the instruction following bl.
        # The site is an address-only equality, not an interpretation
        # of the XDK API's argument ABI or a guarantee of direct EoR ownership.
        site = (lr - 4) & 0xFFFFFFFF
        callers = sorted({caller for caller, callsite in reverse.get(xdk, ())
                          if callsite == site})
        record = {
            'verified_xdk_method': hooks[xdk],
            'xdk_guest_address': f'0x{xdk:08X}',
            'observed_guest_lr': f'0x{lr:08X}',
            'static_direct_bl_site': f'0x{site:08X}' if callers else None,
            'source_defined_guest_callers': [f'0x{x:08X}' for x in callers],
            'observed_calls': row['draws'],
            'observed_frames': row['frames'],
            'verified_direct_bl_link': bool(callers),
            'verified_engine_draw_abi': False,
            'verified_mesh_or_material_binding': False,
            'verified_camera_or_projection': False,
        }
        (matched if callers else unmatched).append(record)
    return {
        'schema': 'hulk_usa_runtime_to_static_xdk_callsite_v1',
        'source_xex_sha256': USA_XEX_SHA256,
        'matched_direct_callsite_count': len(matched),
        'unmatched_callsite_count': len(unmatched),
        'matched': matched,
        'unmatched': unmatched,
        'limitations': [
            'Only direct PPC bl return addresses exactly matching LR-4',
            'PPC bctrl, tail calls, XDK thunks and altered LR remain unmatched',
            'LR=caller instruction proves an XDK boundary, not EoR mesh ownership',
            'No r3-r10, game data, shader or original file bytes are emitted',
            'A valid engine Draw ABI and material/camera ownership still need '
            'independent live guest memory and callsite evidence',
        ],
    }


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--generated', type=Path, required=True)
    p.add_argument('--source-xex', type=Path, required=True)
    p.add_argument('--profile', type=Path, required=True)
    p.add_argument('--log', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    verify_us_source(args.source_xex)
    hooks = read_hooks(args.profile)
    reverse, files, functions, found = scan_direct_calls(
        args.generated, set(hooks))
    # ReXGlue may replace an original imported XDK function with a host
    # hook and omit its *definition* while retaining exact guest bl callsites.
    # Only the latter are necessary for a provable LR-4 crosswalk.
    verified_draw_edges = any(reverse.get(hook) for hook, name in hooks.items()
                              if name in ('DrawVertices', 'DrawIndexedVertices'))
    if files < 1 or not functions or not verified_draw_edges:
        raise ValueError("USA codegen has no verified original XDK Draw callsite edges")
    if args.log.stat().st_size > MAX_TRACE_BYTES:
        raise ValueError("Runtime log is too large for bounded caller analysis")
    result = analyze(args.log.read_text(encoding='utf-8', errors='replace'),
                     hooks, reverse)
    result['verified_usa_source'] = True
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('[HULK/EOR-ABI-CROSSWALK] directly matched guest callers:',
          result['matched_direct_callsite_count'], 'unmatched:',
          result['unmatched_callsite_count'])


if __name__ == '__main__':
    main()
