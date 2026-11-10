#!/usr/bin/env python3
"""Read-only PowerPC call graph around Hulk's eight verified XDK hooks.

Input: generated ReXGlue C++ from the user's SHA-pinned USA default.xex.
Output: address-only candidates for direct engine scene/graphics extraction.
Never treats an ancestor as ABI proof. No guest binaries or PPC bytes in output.
"""
from __future__ import annotations
import argparse
from audit_hulk_xex_xdk import inspect as inspect_xex, verify_xdk_6534
from collections import defaultdict
from configparser import ConfigParser
import json
from pathlib import Path
import re

FUNC = re.compile(r'^DEFINE_REX_FUNC\(sub_([0-9A-Fa-f]{8})\)')
LABEL = re.compile(r'^\s*loc_([0-9A-Fa-f]{8}):')
ASM = re.compile(r'^\s*//\s*bl\s+(?:0x)?([0-9A-Fa-f]{8})\s*$')

USA_XEX_SHA256 = "346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0"


def verify_us_source(path: Path, expected_sha: str = USA_XEX_SHA256) -> dict:
    """Verify the exact user's USA XEX, rather than matching XDK alone."""
    xex = inspect_xex(path.read_bytes())
    verify_xdk_6534(xex)
    if xex['sha256'].lower() != expected_sha.lower():
        raise ValueError(
            "USA XEX SHA-256 mismatch: expected " + expected_sha.lower()
            + ", got " + xex['sha256'])
    return {
        'source_xex_sha256': xex['sha256'],
        'source_title_id': xex['title_id'],
        'source_media_id': xex['media_id'],
        'source_region_mask': xex['game_region_mask'],
        'source_xdk_6534_verified': True,
    }


def read_hooks(ini: Path) -> dict[int, str]:
    cfg = ConfigParser(interpolation=None)
    cfg.optionxform = str
    if not cfg.read(ini, encoding='utf-8') or not cfg.has_section('hooks'):
        raise ValueError('Missing Hulk XDK hook profile')
    hooks: dict[int, str] = {}
    for key, label in cfg.items('hooks'):
        addr = int(key, 0)
        if addr in hooks or not (0x82000000 <= addr < 0x84000000):
            raise ValueError('Duplicate or out-of-image hook address')
        hooks[addr] = label
    if len(hooks) != 8:
        raise ValueError('Pinned Hulk 6534 profile must provide eight hooks')
    return hooks


def scan_direct_calls(
    generated: Path, wanted_hooks: set[int] | None = None
) -> tuple[dict[int, list[tuple[int, int]]], int, int, set[int]]:
    """Reverse direct PPC bl and record exact guest function definitions."""
    reverse: dict[int, list[tuple[int, int]]] = defaultdict(list)
    wanted_hooks = wanted_hooks or set()
    found_hooks: set[int] = set()
    files = sorted(generated.glob('*_recomp.*.cpp'))
    if not files:
        raise ValueError('Missing ReXGlue *_recomp.NUM.cpp source files')
    functions = 0
    for file in files:
        current = None
        address = 0
        with file.open(encoding='utf-8', errors='replace') as stream:
            for line in stream:
                match = FUNC.match(line)
                if match:
                    current = int(match.group(1), 16)
                    address = current
                    functions += 1
                    if current in wanted_hooks:
                        if current in found_hooks:
                            raise ValueError(f"Duplicate USA XDK function 0x{current:08X}")
                        found_hooks.add(current)
                    continue
                if current is None:
                    continue
                match = LABEL.match(line)
                if match:
                    address = int(match.group(1), 16)
                    continue
                stripped = line.strip()
                if not stripped.startswith('// '):
                    continue
                match = ASM.match(line)
                if match:
                    target = int(match.group(1), 16)
                    if target != current:
                        reverse[target].append((current, address))
                asm = stripped[3:]
                if re.match(r'^[A-Za-z][A-Za-z0-9_.]*\s', asm):
                    address += 4
    return reverse, len(files), functions, found_hooks


def analyze(reverse: dict[int, list[tuple[int, int]]],
            hooks: dict[int, str], depth: int = 3,
            found_hooks: set[int] | None = None) -> dict:
    if not (1 <= depth <= 5):
        raise ValueError('Depth must be 1..5')
    evidence: dict[int, dict[str, dict]] = defaultdict(dict)
    per_hook: dict[str, dict] = {}
    for hook, label in hooks.items():
        frontier, visited, levels = {hook}, {hook}, {}
        for level in range(1, depth + 1):
            nxt = set()
            for callee in frontier:
                for caller, site in reverse.get(callee, ()):
                    if caller in visited:
                        continue
                    nxt.add(caller)
                    old = evidence[caller].get(label)
                    if old is None or level < old['distance']:
                        evidence[caller][label] = {
                            'distance': level,
                            'next_callee': f'0x{callee:08X}',
                            'call_site': f'0x{site:08X}',
                        }
            if not nxt:
                break
            levels[str(level)] = len(nxt)
            visited.update(nxt)
            frontier = nxt
        per_hook[label] = {
            'guest_address': f'0x{hook:08X}',
            'present_in_usa_codegen': (
                hook in found_hooks if found_hooks is not None else None),
            'direct_callers': len({c for c, _ in reverse.get(hook, ())}),
            'candidate_ancestors_per_level': levels,
        }

    def group(name):
        if name in ('DrawVertices', 'DrawIndexedVertices'):
            return 'draw'
        if name == 'Resolve':
            return 'resolve'
        if name == 'Present':
            return 'present'
        if name in ('SetRenderTarget', 'SetDepthStencilSurface'):
            return 'rt_bind'
        return 'tiling'

    ranked = []
    for function, links in evidence.items():
        groups = sorted({group(name) for name in links})
        score = (len(groups), len(links), -min(p['distance'] for p in links.values()))
        ranked.append((score, function, groups, links))
    ranked.sort(key=lambda entry: (entry[0], -entry[1]), reverse=True)
    return {
        'schema': 'hulk_powerpc_render_callgraph_v1',
        'max_depth': depth,
        'hooks': per_hook,
        'missing_xdk_function_bodies': (
            [f'0x{address:08X}' for address in sorted(set(hooks) - found_hooks)]
            if found_hooks is not None else []),
        'total_candidates': len(ranked),
        'candidate_ancestors': [
            {'guest_function': f'0x{f:08X}', 'hook_groups': groups, 'evidence': links}
            for _, f, groups, links in ranked[:100]
        ],
        'limitations': [
            'Static bl call sites, not runtime frequency or engine structure proof',
            'Indirect bctrl, virtual dispatch and inlined calls are not resolved',
            'Function address presence does not validate the XDK ABI or layout',
            'Candidates need live tracing against the SHA-pinned USA XEX',
            'This audit emits no executable hooks or renderer changes',
        ],
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--generated', type=Path, required=True)
    p.add_argument('--source-xex', type=Path, required=True)
    p.add_argument('--profile', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--depth', type=int, default=3)
    args = p.parse_args()
    provenance = verify_us_source(args.source_xex)
    hooks = read_hooks(args.profile)
    reverse, file_count, function_count, found = scan_direct_calls(
        args.generated, set(hooks))
    report = analyze(reverse, hooks, depth=args.depth, found_hooks=found)
    report.update(provenance)
    report['source_files'] = file_count
    report['guest_functions'] = function_count
    report['direct_call_sites'] = sum(map(len, reverse.values()))
    report['source_provenance_note'] = (
        'USA XEX SHA verified; this address-only report contains no guest '
        'binary, PPC instructions or shader microcode')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'[HULK/ENGINE-CALLGRAPH] AOT files={file_count}, functions={function_count}, '
          f'call_sites={report["direct_call_sites"]}, ancestors={report["total_candidates"]}')


if __name__ == '__main__':
    main()
