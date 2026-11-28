#!/usr/bin/env python3
"""Transfer the independently verified TU1 vertical-origin comparison only.

No EU code, addresses, or game bytes are installed in the USA game.
Unsupported generated code is rejected before a file is modified.
"""
import argparse
import hashlib
from pathlib import Path
import re

PIN = '346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0'
OWNER = 0x8228D760
MARKER = 'HULK_TU1_WATER_ORIGIN_V1'
FUNC = re.compile(r'^\s*DEFINE_REX_FUNC\(sub_([0-9a-fA-F]{8})\)')
ADDRESS = re.compile(r'^\s*(?:loc_([0-9a-fA-F]{8}):|// Block: 0x([0-9a-fA-F]{8}))\s*$')
ASM = re.compile(r'^\s*// ([a-z][a-z0-9_.+-]*(?:\s+.*?)?)\s*$')
# Verified contiguous USA PPC operations. Decimal operands are codegen's
# own assembly comments; accept optional spaces, not alternate registers.
EXPECTED = {0x8228D7A0: 'stfs f11,216(r31)', 0x8228D7A4: 'lfs f11,84(r11)',
            0x8228D7A8: 'stfs f11,220(r31)', 0x8228D7AC: 'lfs f11,88(r11)'}
STORE = re.compile(r'(\b(?:PPC|REX(?:_MM)?)_STORE_U32\(\s*(?:ctx\.)?r31\.u32\s*\+\s*(?:220|0[xX][dD][cC])\s*,\s*)([A-Za-z_]\w*(?:\.\w+)*\.u32)(\s*\)\s*;)')


def patch(source):
    lines = source.splitlines()
    owner = address = None
    found = {}
    owners = 0
    for i, line in enumerate(lines):
        m = FUNC.match(line)
        if m:
            owner = address = int(m[1], 16)
            owners += owner == OWNER
            continue
        if owner != OWNER:
            continue
        m = ADDRESS.match(line)
        if m:
            address = int(m[1] or m[2], 16)
            continue
        m = ASM.match(line)
        if m:
            if address in EXPECTED:
                if address in found or ''.join(m[1].split()) != ''.join(EXPECTED[address].split()):
                    raise ValueError('Unexpected USA vertical-origin instruction')
                found[address] = i
            address += 4
    if owners != 1 or set(found) != set(EXPECTED):
        raise ValueError('USA vertical-origin instruction sequence absent or ambiguous')
    start, end = found[0x8228D7A8]+1, found[0x8228D7AC]
    if not start < end <= start+12:
        raise ValueError('Unsupported store translation range')
    marked = [i for i,line in enumerate(lines) if MARKER in line]
    if marked:
        if len(marked) != 1 or not start <= marked[0] < end or 'std::bit_cast<float>' not in lines[marked[0]]:
            raise ValueError('Malformed prior TU1 patch')
        return source
    matches = [(i, STORE.search(lines[i])) for i in range(start,end)]
    matches = [(i,m) for i,m in matches if m]
    if len(matches) != 1:
        raise ValueError('Unsupported original translated vertical-origin store')
    i,m = matches[0]
    value = m[2]
    replacement = m[1] + '(std::bit_cast<float>(static_cast<uint32_t>(' + value + ')) > -1.0f ? 0xc0a00000u : ' + value + ')' + m[3]
    lines[i] = lines[i][:m.start()] + replacement + lines[i][m.end():] + ' // '+MARKER
    return '#include <bit>\n#include <cstdint>\n'+'\n'.join(lines)+'\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('generated',type=Path);ap.add_argument('xex',type=Path)
    args=ap.parse_args()
    with args.xex.open('rb') as f:
        if hashlib.file_digest(f,'sha256').hexdigest()!=PIN:
            raise SystemExit('TU1 origin fix rejected: unexpected USA XEX')
    candidates=[]
    for path in args.generated.glob('*_recomp.*.cpp'):
        source=path.read_text(encoding='utf-8')
        if re.search(r'DEFINE_REX_FUNC\(sub_8228[dD]760\)',source) or MARKER in source:
            candidates.append((path,source))
    if len(candidates)!=1:
        print('TU1 origin fix unavailable: expected one USA generated function');return
    path,source=candidates[0]
    try: updated=patch(source)
    except ValueError as exc:
        if MARKER in source:raise SystemExit(str(exc)) from exc
        print('TU1 origin fix unavailable; original preserved:',exc);return
    if updated!=source:path.write_text(updated,encoding='utf-8',newline='\n')
    print('Verified TU1 vertical-origin condition installed in USA generated code')

if __name__=='__main__':main()
