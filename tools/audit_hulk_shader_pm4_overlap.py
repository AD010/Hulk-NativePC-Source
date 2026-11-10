#!/usr/bin/env python3
"""Detect plausible embedded Xbox 360 PM4 packet chains in failed shader dumps.

Reads an existing Hulk NativeXRenderer log; prints only shader hashes, packet
indices and aggregate counts, never copyrighted shader microcode.
A matching PM4 sequence is a diagnostic clue, not proof of memory corruption.

Usage: python tools/audit_hulk_shader_pm4_overlap.py incredible_hulk.log
       python tools/audit_hulk_shader_pm4_overlap.py --test
"""
from __future__ import annotations
import argparse
import re
from pathlib import Path

SHADER_LINE = re.compile(r'vertex shader ([0-9A-F]{16}) \((\d+) dwords\): analysis failed')
DUMP_LINE = re.compile(r'the vertex program came from (\w+) at (0x[0-9a-f]+) '
                       r'\(packet (0x[0-9a-f]+)\).*?: ([0-9A-F ]+)$')
OPCODES = {
    0x2F:'LOAD_ALU_CONSTANT', 0x27:'IM_LOAD', 0x2B:'IM_LOAD_IMMEDIATE',
    0x22:'DRAW_INDX', 0x36:'DRAW_INDX_2', 0x3F:'INDIRECT_BUFFER',
    0x2D:'SET_CONSTANT', 0x55:'SET_CONSTANT2', 0x56:'SET_SHADER_CONSTANTS',
}

def headers(words):
    candidates={}
    for i,word in enumerate(words):
        if word >> 30 != 3:
            continue
        op=(word >> 8)&0x7F
        count=((word >> 16)&0x3FFF)+1
        if op not in OPCODES or count>len(words)-i-1:
            continue
        if op==0x2F and count<3:
            continue
        if op in (0x27,0x2B,0x3F) and count<2:
            continue
        candidates[i]=(op,count)
    chains=[]
    for i,(op,count) in candidates.items():
        next_index=i+1+count
        if next_index in candidates:
            chains.append((i,next_index,op,candidates[next_index][0]))
    return chains,candidates

def analyze(text):
    lines=text.splitlines()
    events=[]
    for line_number,line in enumerate(lines):
        m=SHADER_LINE.search(line)
        if not m:
            continue
        for following in lines[line_number+1:line_number+5]:
            d=DUMP_LINE.search(following)
            if not d:
                continue
            words=[int(s,16) for s in d.group(4).split()]
            chains,candidates=headers(words)
            events.append(dict(shader=m.group(1),size=len(words),declared=int(m.group(2)),
                               method=d.group(1),packet=d.group(3),chains=chains,
                               candidates=sorted(candidates)))
            break
    return events

def summarize(events):
    print(f'Failed shader dumps: {len(events)}')
    print('With adjacent plausible PM4 packets: '
          f'{sum(bool(e["chains"]) for e in events)}')
    for e in events:
        sequences=[f'{a}:{OPCODES[op]} -> {b}:{OPCODES[op2]}'
                   for a,b,op,op2 in e['chains']]
        print(f'{e["shader"]}: {e["size"]}/{e["declared"]} words; '
              f'{len(e["candidates"])} plausible headers; '
              f'chains={sequences or "none"}; packet={e["packet"]}')

def test():
    words=[0]*27
    words[1]=0xC0022F00
    words[5]=0xC0012700
    words[25]=0xC0032201 # invalid: truncated DRAW packet
    chains,found=headers(words)
    assert chains==[(1,5,0x2F,0x27)]
    assert 25 not in found
    synthetic=('vertex shader 1234567890ABCDEF (27 dwords): analysis failed: access violation\n'
               '[native_x_renderer] the vertex program came from IM_LOAD_IMMEDIATE '
               'at 0xa200100c (packet 0xa2001000) in DrawIndexedVertices: '
               + ' '.join(f'{x:08X}' for x in words))
    assert len(analyze(synthetic))==1
    assert len(analyze(synthetic)[0]['chains'])==1
    print('Self-test: PASS')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('log',nargs='?',type=Path)
    p.add_argument('--test',action='store_true')
    args=p.parse_args()
    if args.test:
        test()
    if args.log:
        summarize(analyze(args.log.read_text(encoding='utf-8',errors='replace')))
