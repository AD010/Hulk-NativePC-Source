#!/usr/bin/env python3
"""Transfer the verified EU TU1 regional transition rejection to USA."""
from patch_hulk_tu1_water_origin import PIN, FUNC, ADDRESS, ASM
import re

OWNER = 0x82288580
MARKER = 'HULK_TU1_LEDGE_REGION_V1'
EXPECTED = {0x822885A0:'bl 0x826bd808', 0x822885A4:'li r5,1',
    0x822885A8:'addi r3,r1,112', 0x822885AC:'lwz r4,164(r31)',
    0x822885B0:'lfs f30,272(r1)', 0x822885B4:'lfs f29,276(r1)',
    0x822885B8:'lfs f28,280(r1)', 0x822885BC:'bl 0x825f6938',
    0x8228870C:'li r3,0', 0x82288710:'b 0x822887c8',
    0x822887C8:'addi r1,r1,384'}

def patch(source):
    lines=source.splitlines(); owner=address=None; found={}; owners=0
    for i,line in enumerate(lines):
        m=FUNC.match(line)
        if m:
            owner=address=int(m[1],16); owners+=owner==OWNER; continue
        if owner!=OWNER:continue
        m=ADDRESS.match(line)
        if m:address=int(m[1] or m[2],16);continue
        m=ASM.match(line)
        if m:
            if address in EXPECTED:
                observed=re.sub(r'0x(?=[0-9a-f]{8}$)','', ''.join(m[1].lower().split()))
                expected=EXPECTED[address].lower().replace('0x','').replace(' ','')
                if address in found or observed!=expected:raise ValueError('Unexpected USA regional transition instruction')
                found[address]=i
            address+=4
    if owners!=1 or set(found)!=set(EXPECTED):raise ValueError('USA regional transition not uniquely mapped')
    def operand(a,z,pattern):
        lo,hi=found[a]+1,found[z]
        if not lo<hi<=lo+12:raise ValueError('Unsupported translated regional operand range')
        matches=[(i,re.search(pattern,lines[i])) for i in range(lo,hi)]
        matches=[(i,m) for i,m in matches if m]
        if len(matches)!=1:raise ValueError('Unsupported translated regional operand')
        return matches[0]
    regs=[]
    for a,z,n,offset in [(0x822885B0,0x822885B4,30,272),(0x822885B4,0x822885B8,29,276),(0x822885B8,0x822885BC,28,280)]:
        _,load=operand(a,z,r'([A-Za-z_]\w*(?:\.\w+)*)\.u32\s*=\s*(?:PPC|REX(?:_MM)?)_LOAD_U32\(\s*(?:ctx\.)?r1\.u32\s*\+\s*'+str(offset)+r'\s*\)\s*;')
        at,reg=operand(a,z,r'((?:ctx\.)?f'+str(n)+r'\.f64)\s*=\s*double\(\s*'+re.escape(load[1])+r'\.f32\s*\)\s*;')
        regs.append(reg[1])
    _,zero=operand(0x8228870C,0x82288710,r'((?:ctx\.)?r3\.[su](?:32|64))\s*=\s*0\s*;')
    # The original zero-return branch already restores the exact stack and
    # nonvolatile registers. Reuse its generated target, not a raw return.
    hi=found[0x82288710]+1
    matches=[re.search(r'\bgoto\s+(loc_822887[cC]8)\s*;',line) for line in lines[hi:hi+10]]
    targets=[m[1] for m in matches if m]
    if len(targets)!=1 or sum(line.strip()==targets[0]+':' for line in lines)!=1:raise ValueError('Original return label unavailable')
    x,y,z=regs
    marker=f'  if ({y} < -30.0 && {x} > -3400.0 && {x} < -2600.0 && {z} > 1950.0 && {z} < 2450.0) {{ {zero[1]} = 0; goto {targets[0]}; }} // {MARKER}'
    marked=[i for i,line in enumerate(lines) if MARKER in line]
    if marked:
        if marked!=[at+1] or lines[at+1]!=marker:raise ValueError('Malformed regional fix')
        return source
    lines.insert(at+1,marker)
    return '\n'.join(lines)+'\n'
