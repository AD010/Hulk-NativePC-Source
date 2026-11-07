#!/usr/bin/env python3
"""Identify shared XDK 6534 PPC functions across user-owned Xbox 360 PE images.

Decode/decompress the user's own XEX to PE first. No proprietary game bytes are
bundled with this repository. Similarity never establishes a D3D hook ABI.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import struct


def functions(path: Path, max_size=4096) -> dict[int, tuple[int, ...]]:
    raw=path.read_bytes()
    if len(raw)<0x200 or raw[:2]!=b'MZ':
        raise ValueError('Expected a locally extracted Xbox 360 PE')
    pe=struct.unpack_from('<I',raw,0x3c)[0]
    if pe+24>len(raw) or raw[pe:pe+4]!=b'PE\0\0':
        raise ValueError('Invalid PE signature')
    count=struct.unpack_from('<H',raw,pe+6)[0]
    opt=pe+24
    sh=opt+struct.unpack_from('<H',raw,pe+20)[0]
    if not 1<=count<=40 or sh+count*40>len(raw):
        raise ValueError('Invalid PE section table')
    if struct.unpack_from('<H',raw,opt)[0]!=0x10b:
        raise ValueError('Expected PPC PE32')
    base=struct.unpack_from('<I',raw,opt+28)[0]
    sections={}
    for i in range(count):
        o=sh+40*i
        name=raw[o:o+8].split(b'\0')[0].decode('ascii','replace')
        vsize,va,size,offset=struct.unpack_from('<IIII',raw,o+8)
        if offset+size>len(raw):
            raise ValueError('Truncated PE section')
        sections[name]=(base+va,raw[offset:offset+min(vsize,size)])
    if '.text' not in sections or '.pdata' not in sections:
        raise ValueError('PPC .text/.pdata missing')
    text_base,text=sections['.text']
    _,pdata=sections['.pdata']
    hi=text_base+len(text)
    starts=sorted({struct.unpack_from('>I',pdata,i)[0]
                   for i in range(0,len(pdata)-7,8)
                   if text_base<=struct.unpack_from('>I',pdata,i)[0]<hi})
    result={}
    for j,addr in enumerate(starts):
        size=min(max_size,(starts[j+1] if j+1<len(starts) else hi)-addr)
        if size<24: continue
        words=struct.unpack_from('>'+'I'*(size//4),text,addr-text_base)
        if any(words):result[addr]=words
    return result


def normalized(word:int)->int:
    op=word>>26
    if op==18:return word&0xfc000003   # direct branch target
    if op==16:return word&0xffff0003   # condition preserved, target removed
    if op==15 and 0x8200<=(word&0xffff)<=0x831b:
        return word&0xffff0000           # guest image relocation
    return word


def grams(words):
    values=[normalized(v) for v in words]
    return set(zip(values,values[1:],values[2:]))


def compare(source:dict,reference:dict,addresses:list[int]):
    dictionary=defaultdict(set)
    back={}
    for address,words in reference.items():
        parts=grams(words)
        back[address]=parts
        for part in parts:dictionary[part].add(address)
    results={}
    for address in addresses:
        if address not in source:
            results[f'0x{address:08X}']=[]
            continue
        target=grams(source[address])
        votes=Counter()
        for part in target:
            values=dictionary.get(part,())
            if 0<len(values)<=6:
                votes.update(values)
        rows=[]
        for match,unused in votes.most_common(40):
            common=len(target&back[match])
            rows.append({'guest_address':f'0x{match:08X}',
                         'shared_threegrams':common,
                         'shorter_coverage':round(common/max(1,min(len(target),len(back[match]))),4),
                         'validated_d3d_abi':False})
        rows.sort(key=lambda row:(row['shared_threegrams'],row['shorter_coverage']),reverse=True)
        results[f'0x{address:08X}']=rows[:3]
    return results


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source_pe',type=Path)
    p.add_argument('other_pe',type=Path)
    p.add_argument('--guest-addresses',nargs='+',required=True)
    p.add_argument('--report',type=Path,required=True)
    arg=p.parse_args()
    source=functions(arg.source_pe)
    other=functions(arg.other_pe)
    results={'schema':'hulk_xdk6534_cross_pe_v1',
             'source_function_fragments':len(source),
             'other_function_fragments':len(other),
             'caution':'PPC similarity is not a validated D3D entrypoint or DeviceLayout',
             'candidates':compare(source,other,[int(x,0) for x in arg.guest_addresses])}
    arg.report.parent.mkdir(parents=True,exist_ok=True)
    arg.report.write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
    print(f'Hulk XDK candidate matches: {len(results["candidates"])}; saved {arg.report}')


if __name__=='__main__':
    main()
