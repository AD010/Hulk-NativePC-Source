#!/usr/bin/env python3
"""Instrument only verified original USA selector and channel-start stores.
No guest register or instruction is replaced. Unsupported codegen stays unmodified.
"""
import argparse
import hashlib
import re
from pathlib import Path

PIN = '346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0'
FUNC = re.compile(r'^DEFINE_REX_FUNC\(sub_([0-9a-fA-F]{8})\)')
LABEL = re.compile(r'^\s*loc_([0-9a-fA-F]{8}):')
BLOCK = re.compile(r'^\s*// Block: 0x([0-9a-fA-F]{8})\s*$')
ASM = re.compile(r'^\s*// ([a-z][a-z0-9_.+-]*(?:\s+.*?)?)\s*$')
PREFIX = 'HULK_FMOD_START_TRACE_V2'
LEGACY = 'HULK_FMOD_START_TRACE_V1'


def instructions(lines):
    owner = address = None
    out = []
    prologues = {}
    seen = set()
    for i, line in enumerate(lines):
        m = FUNC.match(line)
        if m:
            owner = address = int(m[1], 16)
            seen.clear()
            continue
        if owner is None:
            continue
        if line.strip() == 'REX_FUNC_PROLOGUE();':
            if owner in prologues:
                raise ValueError('Duplicate function prologue')
            prologues[owner] = i
        m = LABEL.match(line) or BLOCK.match(line)
        if m:
            address = int(m[1], 16)
            continue
        m = ASM.match(line)
        if m:
            if owner in (0x82838650, 0x82839368):
                if address & 3 or address in seen:
                    raise ValueError("Duplicate or unaligned PPC instruction address")
                seen.add(address)
                out.append((owner, address, i, m[1]))
            address += 4
    return out, prologues


def unique(items, message):
    if len(items) != 1:
        raise ValueError(message)
    return items[0]


def translated(lines, index, pattern):
    matches = []
    for i in range(index + 1, min(index + 12, len(lines))):
        line = lines[i]
        if ASM.match(line) or LABEL.match(line) or BLOCK.match(line) or FUNC.match(line):
            break
        m = re.search(pattern, line)
        if m:
            matches.append((i, m))
    return unique(matches, 'Unsupported translated memory operand')


def register(n):
    return r'((?:ctx\.)?r' + n + r'\.u(?:32|64))'


def patch_new_selection(sources):
    lines = {name: src.splitlines() for name, src in sources.items()}
    if any(LEGACY in src for src in sources.values()):
        raise ValueError('Legacy trace patch; regenerate original code')
    markers = [line for seq in lines.values() for line in seq if PREFIX in line]
    if markers:
        if len(markers) != 5 or any(sum(PREFIX + ' ' + tag in line for line in markers) != 1
                                    for tag in ('scope', 'child', 'selection', 'channel', 'success')):
            raise ValueError('Partial/duplicate trace patch; regenerate original code')
        return sources
    rows = []
    prologues = {}
    for name, seq in lines.items():
        ins, pros = instructions(seq)
        rows.extend((name, *row) for row in ins)
        prologues.update({(name, owner): i for owner, i in pros.items()})
    selector = 0x82838650
    child = unique([r for r in rows if r[1] == selector and r[2] == 0x82838664
                    and re.fullmatch(r'lwz r11,\s*76\(r27\)', r[4])], 'Selector parent load not unique')
    selection = unique([r for r in rows if r[1] == selector and r[2] == 0x828388FC
                        and re.fullmatch(r'stw r11,\s*48\(r21\)', r[4])], 'Selector index store not unique')
    channel = unique([r for r in rows if r[1] == 0x82839368 and r[2] == 0x8283960C
                      and re.fullmatch(r'stw r10,\s*20\(r31\)', r[4])], 'Channel flags store not unique')
    success = unique([r for r in rows if r[:2] == channel[:2] and r[2] == 0x82839B94
                      and re.fullmatch(r'li r3,\s*0', r[4])], 'Successful return path missing')
    if child[0] != selection[0] or channel[1] == selector:
        raise ValueError('Unexpected selector/start ownership')
    store_reg, node_reg = re.fullmatch(r'stw r(\d+),\s*20\(r(\d+)\)', channel[4]).groups()
    prior = [r for r in rows if r[:2] == channel[:2] and channel[2]-12 <= r[2] < channel[2]]
    unique([r for r in prior if re.fullmatch(r'ori r'+store_reg+r',\s*r'+store_reg+r',\s*8', r[4])], 'Channel active-bit instruction missing')
    unique([r for r in rows if r[:2] == channel[:2] and r[2] == 0x828393DC
            and re.fullmatch(r'bl\s+(?:0x)?82838650', r[4], re.I)], 'Start does not directly call selector exactly once')
    child_reg = re.fullmatch(r'lwz r\d+,\s*76\(r(\d+)\)', child[4])[1]
    variant_reg, parent_reg = re.fullmatch(r'stw r(\d+),\s*48\(r(\d+)\)', selection[4]).groups()
    ci, cm = translated(lines[child[0]], child[3], r'(?:PPC|REX(?:_MM)?)_LOAD_U32\(\s*'+register(child_reg)+r'\s*\+\s*(?:76|0x4[cC])\s*\)')
    si, sm = translated(lines[selection[0]], selection[3], r'(?:PPC|REX(?:_MM)?)_STORE_U32\(\s*'+register(parent_reg)+r'\s*\+\s*(?:48|0x30)\s*,\s*'+register(variant_reg)+r'\s*\)\s*;')
    fi, fm = translated(lines[channel[0]], channel[3], r'(?:PPC|REX(?:_MM)?)_STORE_U32\(\s*'+register(node_reg)+r'\s*\+\s*(?:20|0x14)\s*,\s*'+register(store_reg)+r'\s*\)\s*;')
    ri, _ = translated(lines[success[0]], success[3], r'(?:ctx\.)?r3\.[su](?:32|64)\s*=\s*0\s*;')
    edits = {}
    def add(name, at, value):
        edits.setdefault(name, []).append((at, value))
    sp = prologues.get((child[0], selector));fp = prologues.get(channel[:2])
    if sp is None or fp is None or sp >= child[3] or fp >= channel[3]:
        raise ValueError('Missing original function prologue')
    add(child[0], sp+1, '  uint32_t hulk_fmod_trace_child = 0;')
    add(channel[0], fp+1, '  uint32_t hulk_fmod_trace_started_child = 0;\n  ::hulk::eor::audio::FmodStartTraceScope hulk_fmod_trace_scope(base); // '+PREFIX+' scope')
    add(child[0], ci, '  hulk_fmod_trace_child = static_cast<uint32_t>('+cm[1]+');\n  ::hulk::eor::xdk::TraceFmodSelectionBegin(base, hulk_fmod_trace_child); // '+PREFIX+' child')
    add(selection[0], si+1, '  ::hulk::eor::xdk::TraceFmodSelectionStore(base, hulk_fmod_trace_child, static_cast<uint32_t>('+sm[1]+'), static_cast<uint32_t>('+sm[2]+')); // '+PREFIX+' selection')
    add(channel[0], fi+1, '  hulk_fmod_trace_started_child = static_cast<uint32_t>('+fm[1]+'); // '+PREFIX+' channel')
    add(success[0], ri+1, '  if (hulk_fmod_trace_started_child) ::hulk::eor::xdk::TraceFmodChannelStart(base, hulk_fmod_trace_started_child); // '+PREFIX+' success')
    result = dict(sources)
    for name, changes in edits.items():
        seq = lines[name]
        for at, value in sorted(changes, reverse=True):
            seq.insert(at, value)
        seq.insert(0, '#include "eor_fmod_start_trace_bridge.h"')
        result[name] = '\n'.join(seq)+'\n'
    return result


CACHE = 'HULK_FMOD_CACHED_RECORD_V1'

def patch_cached_record(sources):
    lines = {name: src.splitlines() for name, src in sources.items()}
    rows = [(name, *row) for name, seq in lines.items() for row in instructions(seq)[0]]
    def exact(addr, asm):
        return unique([r for r in rows if r[1] == 0x82838650 and r[2] == addr
                       and re.fullmatch(asm, r[4])], 'Cached record instruction mismatch')
    load = exact(0x828387A0, r'lwz r11,\s*0\(r22\)')
    exact(0x828387A4, r'cmpwi cr6,\s*r11,\s*-1|cmpwi 6,\s*r11,\s*-1')
    exact(0x828387BC, r'mulli r10,\s*r11,\s*24')
    i, m = translated(lines[load[0]], load[3], register('11')+r'\s*=\s*(?:PPC|REX(?:_MM)?)_LOAD_U32\(\s*(?:ctx\.)?r22\.u32(?:\s*\+\s*0)?\s*\)\s*;')
    seq = lines[load[0]]
    marker = '  ::hulk::eor::xdk::TraceFmodCachedRecord(base, hulk_fmod_trace_child, static_cast<uint32_t>('+m[1]+')); // '+CACHE
    existing = [(name,j) for name, q in lines.items() for j,line in enumerate(q) if CACHE in line]
    if existing:
        if existing != [(load[0],i+1)] or seq[i+1] != marker:raise ValueError('Invalid cached record marker')
        return sources
    seq.insert(i+1,marker)
    result=dict(sources);result[load[0]]='\n'.join(seq)+'\n';return result


def patch(sources):
    return patch_cached_record(patch_new_selection(sources))

def main():
    p = argparse.ArgumentParser();p.add_argument('generated', type=Path);p.add_argument('xex', type=Path)
    args = p.parse_args()
    with args.xex.open('rb') as f:
        if hashlib.file_digest(f, 'sha256').hexdigest() != PIN:
            raise SystemExit('FMOD trace rejected: unexpected source XEX')
    sources = {}
    for file in args.generated.glob('*_recomp.*.cpp'):
        source = file.read_text(encoding='utf-8')
        if PREFIX in source or LEGACY in source or any(int(m[1],16) in (0x82838650, 0x82839368) for m in re.finditer(FUNC.pattern, source, re.M)):
            sources[file] = source
    try:
        updated = patch(sources)
    except ValueError as exc:
        if any(PREFIX in s or LEGACY in s for s in sources.values()):
            raise SystemExit(str(exc)) from exc
        print('FMOD trace unavailable; original playback unchanged:', exc)
        return
    for file, content in updated.items():
        if content != sources[file]:
            file.write_text(content, encoding='utf-8', newline='\n')
    print('FMOD selection and channel-start trace installed/verified (original playback retained)')


if __name__ == '__main__':
    main()
