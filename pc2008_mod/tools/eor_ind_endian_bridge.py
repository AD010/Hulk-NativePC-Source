#!/usr/bin/env python3
"""Reversible Hulk PC/Xbox EoR index byte-order adapter, metadata ONLY.

Does NOT convert Xbox ARC bodies, shader bytecode, model formats or textures.
Never install the resulting file as original 2008 PC index.ind.
"""
import argparse
from pathlib import Path
import struct

def convert(data:bytes,source:str,target:str)->bytes:
    if source not in ('<','>') or target not in ('<','>') or not 24<=len(data)<=8<<20:
        raise ValueError('Invalid source/target or EoR index length')
    n=struct.unpack_from(source+'I',data)[0]
    if n<2 or n>128 or n%2:raise ValueError('Unknown EoR group count')
    offsets=struct.unpack_from(source+f'{n+1}I',data,4)
    if offsets[0]!=4+4*(n+1) or offsets[-1]!=len(data)-4 or any(a>=b for a,b in zip(offsets,offsets[1:])):
        raise ValueError('Invalid index group offsets')
    if struct.unpack_from(source+'I',data,len(data)-4)[0]!=1000:
        raise ValueError('Unexpected original EoR footer')
    output=bytearray(data)
    struct.pack_into(target+'I',output,0,n)
    struct.pack_into(target+f'{n+1}I',output,4,*offsets)
    names=set()
    for i in range(n//2):
        a,b,c=offsets[2*i:2*i+3]
        name=data[a:b]
        if len(name)%4 or b'\0' not in name or any(name[name.index(0)+1:]):
            raise ValueError('Invalid group name/padding')
        title=name[:name.index(0)].decode('ascii')
        if not title or title in names:raise ValueError('Duplicate/empty group name')
        names.add(title)
        if c-b<4:raise ValueError('Truncated group')
        count=struct.unpack_from(source+'I',data,b)[0]
        if count>100000 or c-b!=4+16*count:raise ValueError('Invalid group item count')
        struct.pack_into(target+'I',output,b,count)
        ids=struct.unpack_from(source+f'{count}I',data,b+4) if count else ()
        if any(x>=y for x,y in zip(ids,ids[1:])):raise ValueError('Duplicate resource hash')
        if count:struct.pack_into(target+f'{count}I',output,b+4,*ids)
        for j in range(count):
            at=b+4+4*count+12*j
            offset,size,reserved=struct.unpack_from(source+'III',data,at)
            if size and (offset<4 or offset+size>0xffffffff):
                raise ValueError('Invalid original ARC extent')
            struct.pack_into(target+'III',output,at,offset,size,reserved)
    struct.pack_into(target+'I',output,len(data)-4,1000)
    return bytes(output)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',required=True,type=Path)
    p.add_argument('--source-platform',choices=['pc','xbox'],required=True)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--i-understand-metadata-only',action='store_true')
    a=p.parse_args()
    if not a.i_understand_metadata_only:
        p.error('Refusing to write an unsafe game index; --i-understand-metadata-only required')
    raw=a.source.read_bytes()
    original='<' if a.source_platform=='pc' else '>'
    dest='>' if original=='<' else '<'
    converted=convert(raw,original,dest)
    if convert(converted,dest,original)!=raw:
        raise ValueError('Original index not recovered in round trip')
    a.output.write_bytes(converted)
    print('Metadata-only endian conversion finished. DO NOT INSTALL AS PC index.ind.')
if __name__=='__main__':main()
