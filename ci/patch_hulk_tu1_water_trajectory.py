#!/usr/bin/env python3
"""Transfer the independently compared TU1 water trajectory arithmetic.

Only matched USA instructions are edited; guest layouts and branch targets
stay those of the USA executable. No generated guest source is distributed.
"""
import argparse
import hashlib
from pathlib import Path
import re
from patch_hulk_tu1_water_origin import PIN, FUNC, ADDRESS, ASM

OWNER=0x8228DD18
MARKER='HULK_TU1_WATER_TRAJECTORY_V1'
EXPECTED={
    0x8228E254:'lfs f0,84(r4)', 0x8228E258:'fsubs f12,f11,f0',
    0x8228E264:'fmuls f0,f12,f0', 0x8228E268:'lfs f12,4648(r11)',
    0x8228E2E0:'lfs f13,192(r31)', 0x8228E2E4:'lfs f10,220(r31)',
    0x8228E2E8:'fmuls f12,f12,f0', 0x8228E2EC:'fnmsubs f0,f12,f0,f31',
    0x8228E2F0:'fmuls f0,f0,f13', 0x8228E2F4:'stfs f0,196(r31)',
    0x8228E2F8:'fsubs f13,f11,f0', 0x8228E2FC:'stfs f13,200(r31)',
    0x8228E300:'lwz r11,0(r4)', 0x8228E304:'fadds f29,f10,f0',
    0x8228E308:'lwz r11,264(r11)'}

def freg(n):return r'((?:ctx\.)?f'+str(n)+r'\.f64)'

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
                if address in found or ''.join(m[1].split())!=''.join(EXPECTED[address].split()):
                    raise ValueError('Unexpected USA trajectory instruction')
                found[address]=i
            address+=4
    if owners!=1 or set(found)!=set(EXPECTED):raise ValueError('USA trajectory sequence absent or ambiguous')
    def region(a,z):
        lo,hi=found[a]+1,found[z]
        if not lo<hi<=lo+12:raise ValueError('Unsupported translated instruction range')
        return range(lo,hi)
    regions=[region(0x8228E264,0x8228E268),region(0x8228E2F0,0x8228E2F4),region(0x8228E2F4,0x8228E2F8),region(0x8228E304,0x8228E308)]
    marked=[i for i,line in enumerate(lines) if MARKER in line]
    if marked:
        if len(marked)!=4 or any(sum(i in rg for i in marked)!=1 for rg in regions):raise ValueError('Incomplete trajectory patch')
        return source
    def match(a,z,pattern):
        matches=[(i,re.search(pattern,lines[i])) for i in region(a,z)]
        matches=[(i,m) for i,m in matches if m]
        if len(matches)!=1:raise ValueError('Unsupported trajectory operand translation')
        return matches[0]
    _,actor=match(0x8228E254,0x8228E258,r'\b((?:PPC|REX(?:_MM)?)_LOAD_U32\(\s*(?:ctx\.)?r4\.u32\s*\+\s*(?:84|0[xX]54)\s*\))')
    i,scale=match(0x8228E264,0x8228E268,freg(0)+r'\s*=\s*double\(float\('+freg(12)+r'\s*\*\s*'+freg(0)+r'\)\)\s*;')
    j,target=match(0x8228E2F0,0x8228E2F4,freg(0)+r'\s*=\s*double\(float\('+freg(0)+r'\s*\*\s*'+freg(13)+r'\)\)\s*;')
    k,store=match(0x8228E2F4,0x8228E2F8,r'\b((?:PPC|REX(?:_MM)?)_STORE_U32\(\s*(?:ctx\.)?r31\.u32\s*\+\s*(?:196|0[xX][cC]4)\s*,\s*)([A-Za-z_]\w*(?:\.\w+)*\.u32)(\s*\)\s*;)')
    _,store_float=match(0x8228E2F4,0x8228E2F8,r'([A-Za-z_]\w*(?:\.\w+)*)\.f32\s*=\s*float\('+freg(0)+r'\)\s*;')
    _,error=match(0x8228E2F8,0x8228E2FC,freg(13)+r'\s*=\s*double\(float\('+freg(11)+r'\s*-\s*'+freg(0)+r'\)\)\s*;')
    _,error_store=match(0x8228E2FC,0x8228E300,r'\b(?:PPC|REX(?:_MM)?)_STORE_U32\(\s*(?:ctx\.)?r31\.u32\s*\+\s*(?:200|0[xX][cC]8)\s*,\s*[A-Za-z_]\w*(?:\.\w+)*\.u32\s*\)\s*;')
    l,height=match(0x8228E304,0x8228E308,freg(29)+r'\s*=\s*double\(float\('+freg(10)+r'\s*\+\s*'+freg(0)+r'\)\)\s*;')
    if len({scale[1],scale[3],target[1],target[2],store_float[2],error[3],height[3]})!=1 or store_float[1]+'.u32'!=store[2] or target[3]!=error[1]:
        raise ValueError('Inconsistent translated FPR ownership')
    def replace(at,m,text):lines[at]=lines[at][:m.start()]+text+lines[at][m.end():]+' // '+MARKER
    replace(i,scale,f'{scale[1]} = double(float({scale[2]} * 2.0));')
    replace(j,target,f'{target[1]} = double(float(std::fma({target[2]}, {target[3]}, {height[2]})));')
    # Original f11 was the previous height; TU1 reads the current actor Y.
    # Reuse that register for the subsequent original error subtraction.
    replace(k,store,f'{store[2]} = {actor[1]}; {error[2]} = double(std::bit_cast<float>(static_cast<uint32_t>({store[2]}))); {store[1]}{store[2]}{store[3]}')
    replace(l,height,f'{height[1]} = {height[3]};')
    return '#include <bit>\n#include <cmath>\n#include <cstdint>\n'+'\n'.join(lines)+'\n'


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('generated',type=Path);ap.add_argument('xex',type=Path);args=ap.parse_args()
    with args.xex.open('rb') as f:
        if hashlib.file_digest(f,'sha256').hexdigest()!=PIN:raise SystemExit('TU1 trajectory fix rejected: unexpected USA XEX')
    candidates=[]
    for path in args.generated.glob('*_recomp.*.cpp'):
        source=path.read_text(encoding='utf-8')
        if re.search(r'DEFINE_REX_FUNC\(sub_8228[dD][dD]18\)',source) or MARKER in source:candidates.append((path,source))
    if len(candidates)!=1:print('TU1 trajectory fix unavailable: expected one USA generated function');return
    path,source=candidates[0]
    try:updated=patch(source)
    except ValueError as exc:
        if MARKER in source:raise SystemExit(str(exc)) from exc
        print('TU1 trajectory fix unavailable; original preserved:',exc);return
    if updated!=source:path.write_text(updated,encoding='utf-8',newline='\n')
    print('Verified TU1 vertical trajectory arithmetic installed in USA generated code')

if __name__=='__main__':main()
