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
    private_words=[]
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
            # This is a clue, NOT a command decoder or permission to
            # discard a shader. Guest shader bytes may coincidentally
            # resemble Type3 PM4 headers.
            embedded_im_load = [i for i,(op,count) in candidates.items()
                                if op in (0x27,0x2B) and i >= len(words)//2]
            events.append(dict(shader=m.group(1),size=len(words),declared=int(m.group(2)),
                               method=d.group(1),packet=d.group(3),chains=chains,
                               candidates=sorted(candidates),
                               embedded_shader_load_offsets=embedded_im_load,
                               varying_word_offsets=[],
                               classification="candidate_only_not_verified"))
            private_words.append(words)
            break
    # HULK/SHADER-TAIL-203: detect a family of supposedly different 27-word
    # microcodes that differs only at a plausible embedded PM4 data word.
    # No literal shader words are returned or written to public source.
    for i, words in enumerate(private_words):
        if len(words) < 3: continue
        siblings=[other for j,other in enumerate(private_words)
                  if j != i and len(other)==len(words)]
        if not siblings: continue
        diffs=sorted(set(k for other in siblings
                         for k,(a,b) in enumerate(zip(words,other)) if a!=b))
        if diffs and len(diffs) <= 4:
            events[i]["varying_word_offsets"]=diffs
            if events[i]["embedded_shader_load_offsets"] and (
                    set(diffs).issubset(set(k+1 for k in
                          events[i]["embedded_shader_load_offsets"]))):
                events[i]["classification"]="candidate_pm4_in_shader_tail"
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
              f'chains={sequences or "none"}; packet={e["packet"]}; '
              f'tail_loads={e["embedded_shader_load_offsets"]}; '
              f'varying_offsets={e["varying_word_offsets"]}; '
              f'classification={e["classification"]}')

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
    # Three *synthetic* distinct guest programs with a Type3 IM_LOAD
    # header at index 22 and a changing word at 23, like log203.
    # They must be classified, never discarded or rewritten.
    programs=[]
    for index in range(3):
        original=[0]*27
        original[22]=0xC0012700  # 2-payload-word Type3 IM_LOAD
        original[23]=0x022AB000 + (index << 12)
        original[24]=45
        programs.append('vertex shader %016X (27 dwords): analysis failed: access violation\n'
                        'the vertex program came from IM_LOAD_IMMEDIATE at 0xa200100c '
                        '(packet 0xa2001000) in DrawIndexedVertices: ' %
                        (0xABC0000000000000+index) +
                        ' '.join('%08X' % w for w in original))
    reports=analyze('\n'.join(programs))
    assert len(reports)==3, reports
    assert all(e['embedded_shader_load_offsets']==[22] for e in reports)
    assert all(e['varying_word_offsets']==[23] for e in reports)
    assert all(e['classification']=='candidate_pm4_in_shader_tail' for e in reports)
    assert not any('words' in event for event in reports)
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
