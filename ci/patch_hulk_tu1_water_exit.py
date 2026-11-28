#!/usr/bin/env python3
"""Port the verified TU1 actor state-byte clear after the water transition."""
import argparse
import hashlib
from pathlib import Path
import re
from patch_hulk_tu1_water_origin import PIN,FUNC,ADDRESS,ASM

OWNER=0x8228DD18
MARKER='HULK_TU1_WATER_EXIT_V1'
EXPECTED={0x8228E388:'stb r9,138(r11)',0x8228E38C:'lwz r4,164(r31)',
          0x8228E390:'stb r8,241(r31)',0x8228E394:'mr r3,r4',
          0x8228E39C:'lwz r11,164(r31)',0x8228E3B8:'mr r3,r31',
          0x8228E3BC:'bl 0x82155750',0x8228E3C0:'b 0x8228e590'}


def patch(source):
    lines=source.splitlines();owner=address=None;found={};owners=0
    for i,line in enumerate(lines):
        m=FUNC.match(line)
        if m:
            owner=address=int(m[1],16);owners+=owner==OWNER;continue
        if owner!=OWNER:continue
        m=ADDRESS.match(line)
        if m:
            address=int(m[1] or m[2],16);continue
        m=ASM.match(line)
        if m:
            if address in EXPECTED:
                if address in found or ''.join(m[1].lower().split())!=''.join(EXPECTED[address].lower().split()):
                    raise ValueError('Unexpected original water transition instruction')
                found[address]=i
            address+=4
    if owners!=1 or set(found)!=set(EXPECTED):raise ValueError('Water transition sequence absent or ambiguous')
    def match(a,z,pattern):
        lo,hi=found[a]+1,found[z]
        if not lo<hi<=lo+12:raise ValueError('Unsupported translation range')
        ms=[(i,re.search(pattern,lines[i])) for i in range(lo,hi)]
        ms=[(i,m) for i,m in ms if m]
        if len(ms)!=1:raise ValueError('Unsupported water transition operand')
        return ms[0]
    _,load=match(0x8228E38C,0x8228E390,r'\b((?:PPC|REX(?:_MM)?)_LOAD_U32\(\s*(?:ctx\.)?r31\.u32\s*\+\s*(?:164|0[xX][aA]4)\s*\))')
    _,store=match(0x8228E390,0x8228E394,r'\b((?:PPC|REX(?:_MM)?)_STORE_U8)\(\s*(?:ctx\.)?r31\.u32\s*\+\s*(?:241|0[xX][fF]1)\s*,\s*(?:ctx\.)?r8\.u(?:8|32|64)\s*\)\s*;')
    i,_=match(0x8228E3BC,0x8228E3C0,r'\b(?:sub_|__imp__sub_)82155750\(\s*ctx\s*,\s*base\s*\)\s*;')
    marker='  '+store[1]+'('+load[1]+' + 1005u, 0u); // '+MARKER
    marked=[j for j,line in enumerate(lines) if MARKER in line]
    if marked:
        if marked!=[i+1] or lines[i+1]!=marker:raise ValueError('Malformed prior water exit patch')
        return source
    lines.insert(i+1,marker)
    return '\n'.join(lines)+'\n'


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('generated',type=Path);ap.add_argument('xex',type=Path);args=ap.parse_args()
    with args.xex.open('rb') as f:
        if hashlib.file_digest(f,'sha256').hexdigest()!=PIN:raise SystemExit('TU1 water exit rejected: unexpected USA XEX')
    candidates=[]
    for path in args.generated.glob('*_recomp.*.cpp'):
        source=path.read_text(encoding='utf-8')
        if re.search(r'DEFINE_REX_FUNC\(sub_8228[dD][dD]18\)',source) or MARKER in source:candidates.append((path,source))
    if len(candidates)!=1:print('TU1 water exit unavailable: expected one USA generated function');return
    path,source=candidates[0]
    try:updated=patch(source)
    except ValueError as exc:
        if MARKER in source:raise SystemExit(str(exc)) from exc
        print('TU1 water exit unavailable; original preserved:',exc);return
    if updated!=source:path.write_text(updated,encoding='utf-8',newline='\n')
    print('Verified TU1 actor transition flag clear installed in USA generated code')

if __name__=='__main__':main()
