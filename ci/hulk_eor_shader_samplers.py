#!/usr/bin/env python3
"""Original XDK EoR source shader sampler names/registers -> private HEORSMP1.

Reads the user's SHA-verified original Xbox ShaderEffects/Xenos containers.
A register_set=3 source sampler slot is not automatically the SPIR-V
XeTexture fetch constant or a live EoR material/texture owner. No original
shader instructions or materials are copied into the public repository.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile

MAGIC=b'HEORSMP1'
NAME=re.compile(rb'^[A-Za-z_][A-Za-z0-9_]*$')
HEX=re.compile(r'^[0-9a-f]{64}$')


def inspect_original_samplers(source:bytes,stage:str)->tuple[tuple[int,str],...]:
    if len(source)<68 or stage not in ('VS','PS'):
        raise ValueError('Unsupported original Xenos source/stage')
    flags,virtual,physical,_,constant_at,_,_,_,_=struct.unpack_from('>9I',source)
    if ((flags&0xffffff00)!=0x102a1100 or
            ('VS' if flags&1 else 'PS')!=stage or
            virtual<36 or physical==0 or virtual+physical!=len(source) or
            not 36<=constant_at<virtual or constant_at+4+28>virtual):
        raise ValueError('Original XDK shader header/constant table invalid')
    _,_,_,n,records_at,_,_=struct.unpack_from('>7I',source,constant_at+4)
    if (n>1024 or records_at>virtual-(constant_at+4) or
            n*20>virtual-(constant_at+4+records_at)):
        raise ValueError('Original XDK shader reflection bounds invalid')
    names=set()
    slots=set()
    out=[]
    for i in range(n):
        at=constant_at+4+records_at+i*20
        name_at,register_set,index,count,_,_,_=struct.unpack_from(
            '>IHHHHII',source,at)
        if register_set!=3:
            continue
        off=constant_at+4+name_at
        if off>=virtual:
            raise ValueError('Original sampler name out of shader bounds')
        end=source.find(b'\0',off,min(virtual,off+33))
        if (end<=off or index>=32 or count!=1 or
                not NAME.fullmatch(source[off:end])):
            raise ValueError('Unverified original XDK sampler slot/name')
        name=source[off:end].decode('ascii')
        if index in slots or name in names:
            raise ValueError('Duplicate original XDK shader sampler declaration')
        names.add(name)
        slots.add(index)
        out.append((index,name))
    return tuple(sorted(out))


def generate(manifest:Path,destination:Path,repo_root:Path)->dict:
    manifest=manifest.resolve()
    destination=destination.resolve()
    root=repo_root.resolve()
    if destination==root or root in destination.parents:
        raise ValueError('Original shader sampler metadata must remain private')
    data=json.loads(manifest.read_text(encoding='utf-8'))
    sources=data.get('shader_containers')
    if (data.get('bank_version')!=1 or data.get('platform')!='Xbox360' or
            not isinstance(sources,list) or not 0<len(sources)<=16384):
        raise ValueError('Original Xenos shader manifest missing/invalid')
    found={}
    records=[]
    sampler_count=0
    for entry in sources:
        digest,stage=entry.get('sha256'),entry.get('stage')
        if (not isinstance(digest,str) or not HEX.fullmatch(digest) or
                stage not in ('VS','PS')):
            raise ValueError('Original shader source identity invalid')
        original=(manifest.parent/'Xenos'/(digest+'.xenos.bin')).read_bytes()
        if (len(original)!=entry.get('bytes') or
                hashlib.sha256(original).hexdigest()!=digest):
            raise ValueError('Original Xbox shader source digest/size changed')
        if digest in found:
            if found[digest]!=stage:
                raise ValueError('Original shader changes stage')
            continue
        found[digest]=stage
        slots=inspect_original_samplers(original,stage)
        sampler_count+=len(slots)
        # One sentinel record authenticates a shader with ZERO samplers.
        # A missing digest must never mean a verified texture-less shader.
        for index,name in (slots or ((0xffffffff,''),)):
            encoded=name.encode('ascii')
            record=(bytes.fromhex(digest) +
                    struct.pack('<II',1 if stage=='VS' else 2,index) +
                    encoded.ljust(32,b'\0'))
            assert len(record)==72
            records.append((digest,index,record))
    records.sort(key=lambda row:(row[0],row[1]))
    if len(records)>131072:
        raise ValueError('Unreasonably large original shader sampler map')
    payload=bytearray(MAGIC+struct.pack('<II',len(records),1))
    for _,_,record in records:
        payload.extend(record)
    destination.parent.mkdir(parents=True,exist_ok=True)
    tmp=None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent,
                                         prefix='.hulk_samplers_',delete=False) as f:
            tmp=f.name
            f.write(payload)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp,destination)
    finally:
        if tmp:Path(tmp).unlink(missing_ok=True)
    return {'format':'HEORSMP1','source_shaders':len(found),
            'source_sampler_slots':sampler_count,
            'zero_sampler_shaders':len(records)-sampler_count,
            'private_bank_sha256':hashlib.sha256(payload).hexdigest(),
            'original_material_owner_verified':False,
            'original_spirv_fetch_slot_mapping_verified':False}


def main()->None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,required=True)
    p.add_argument('--private-output',type=Path,required=True)
    a=p.parse_args()
    print(json.dumps(generate(a.manifest,a.private_output,
                              Path(__file__).resolve().parents[1]),
                     sort_keys=True))


if __name__=='__main__':
    main()
