#!/usr/bin/env python3
"""Read-only EoR PC D3D9 / Xbox Xenos resource comparison by original index."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

def parse_index(blob):
    if not 24 <= len(blob) <= 8<<20:
        raise ValueError('Unsafe EoR index length')
    le,be=struct.unpack_from('<I',blob)[0],struct.unpack_from('>I',blob)[0]
    l=2<=le<=128 and le%2==0
    b=2<=be<=128 and be%2==0
    if l==b:raise ValueError('Ambiguous EoR index endianness')
    endian,count=('<',le) if l else ('>',be)
    offsets=struct.unpack_from(endian+str(count+1)+'I',blob,4)
    if (offsets[-1]!=len(blob)-4 or offsets[0]<4+(count+1)*4 or
        any(a>=b for a,b in zip(offsets,offsets[1:]))):
        raise ValueError('Malformed EoR category boundaries')
    result={}
    for i in range(count//2):
        name=blob[offsets[2*i]:offsets[2*i+1]].split(b'\0')[0].decode('ascii')
        if not name or name in result or '/' in name or '\\' in name:
            raise ValueError('Unsafe EoR category')
        start,end=offsets[2*i+1],offsets[2*i+2]
        entries=struct.unpack_from(endian+'I',blob,start)[0]
        if entries>100000 or end-start!=4+16*entries:
            raise ValueError('EoR table length mismatch')
        keys=struct.unpack_from(endian+str(entries)+'I',blob,start+4) if entries else ()
        if any(a>=b for a,b in zip(keys,keys[1:])):
            raise ValueError('Unsorted EoR resource hashes')
        pos=start+4+4*entries
        result[name]={key:struct.unpack_from(endian+'III',blob,pos+j*12)
                      for j,key in enumerate(keys)}
    return 'PC_LE' if endian=='<' else 'XBOX360_BE',result

def audit(pc_index,xbox_index,pc_shaders,xbox_shaders):
    pc_end,pc=parse_index(pc_index)
    xb_end,xb=parse_index(xbox_index)
    if pc_end!='PC_LE' or xb_end!='XBOX360_BE':
        raise ValueError('Swapped original PC/Xbox resource index')
    categories={}
    for name in sorted(pc.keys()|xb.keys()):
        a=set(pc.get(name,{}));b=set(xb.get(name,{}))
        categories[name]={'pc':len(a),'xbox360':len(b),'shared_ids':len(a&b)}
    for index,data,platform in ((pc,pc_shaders,'PC'),(xb,xbox_shaders,'Xbox360')):
        effects=index.get('ShaderEffects')
        if not effects:raise ValueError(platform+' ShaderEffects absent')
        for offset,size,reserved in effects.values():
            if offset<4 or size<=0 or offset+size>len(data) or reserved:
                raise ValueError(platform+' ShaderEffects archive/index mismatch')
    pc_effects=pc['ShaderEffects'];xb_effects=xb['ShaderEffects']
    pc_ctab=sum(pc_shaders[o:o+n].count(b'CTAB') for o,n,_ in pc_effects.values())
    xb_xenos=sum(xbox_shaders[o:o+n].count(b'\x10\x2a\x11') for o,n,_ in xb_effects.values())
    return {
        'pc_index_sha256':hashlib.sha256(pc_index).hexdigest(),
        'xbox_index_sha256':hashlib.sha256(xbox_index).hexdigest(),
        'pc_effects_sha256':hashlib.sha256(pc_shaders).hexdigest(),
        'xbox_effects_sha256':hashlib.sha256(xbox_shaders).hexdigest(),
        'categories':categories,
        'pc_d3d9_ctab_markers':pc_ctab,
        'xbox_xenos_container_markers':xb_xenos,
        'binary_archive_swap_safe':False,
        'reason':'PC D3D9 CTAB shader bytecode and Xbox Xenos shader containers have different ABI/material passes.'
    }

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('pc-index','xbox-index','pc-effects','xbox-effects'):
        p.add_argument('--'+name,required=True,type=Path)
    a=p.parse_args()
    print(json.dumps(audit(a.pc_index.read_bytes(),a.xbox_index.read_bytes(),
                           a.pc_effects.read_bytes(),a.xbox_effects.read_bytes()),
                     indent=2,sort_keys=True))
if __name__=='__main__':main()
