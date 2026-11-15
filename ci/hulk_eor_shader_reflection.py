#!/usr/bin/env python3
"""Extract original ShaderEffects matrix constant REGISTER LOCATIONS privately.

Reads Xenos ShaderContainer reflection in big-endian format. Never exports
original shader instruction bytes, game shaders or texture payloads to Git.
HEORREF1 entries are keyed by original SHA-256. No guessed camera pointer.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile

MAGIC = b'HEORREF1'
MATRICES = ('ViewProj', 'World', 'PrevWorld', 'SkyViewProj')
ABSENT = (0xffffffff, 0)

def inspect_container(source: bytes, stage: str) -> tuple[tuple[int,int],...]:
    if stage not in ('VS','PS') or len(source) < 68:
        raise ValueError('Bad original Xenos shader size/stage')
    flags, virtual, physical, _, constant_at, _, _, _, _ = struct.unpack_from('>9I',source)
    if ((flags & 0xffffff00) != 0x102a1100 or
            ('VS' if flags & 1 else 'PS') != stage or
            virtual < 36 or physical == 0 or virtual+physical != len(source) or
            not 36 <= constant_at < virtual):
        raise ValueError('Invalid source shader container')
    if constant_at+4+28 > virtual:
        raise ValueError('Missing original shader constant table')
    _, _, _, count, records_at, _, _ = struct.unpack_from('>7I',source,constant_at+4)
    if count > 1024 or records_at > virtual-(constant_at+4) or (
            count*20 > virtual-(constant_at+4+records_at)):
        raise ValueError('Out-of-bounds original constant reflection')
    names:dict[str,tuple[int,int]] = {}
    for i in range(count):
        start = constant_at+4+records_at+20*i
        name_at,register_set,index,n,_,_,_ = struct.unpack_from('>IHHHHII',source,start)
        offset = constant_at+4+name_at
        if offset >= virtual:
            raise ValueError('Original constant name outside virtual image')
        end = source.find(b'\0', offset, min(offset+128, virtual))
        if end < 0:
            raise ValueError('Unterminated original shader constant name')
        name = source[offset:end].decode('ascii')
        if name in MATRICES:
            if name in names or register_set != 2 or n != 4 or index > 252:
                raise ValueError('Unexpected original matrix binding')
            # Game shaders use ViewProj in eight PS variants too, but a
            # fragment binding cannot prove the 3D vertex projection.
            if stage == 'VS':
                names[name] = (index,n)
    return tuple(names.get(name,ABSENT) for name in MATRICES)

def generate(manifest:Path,output:Path,repo_root:Path)->dict:
    manifest=manifest.resolve();output=output.resolve();repo_root=repo_root.resolve()
    if output==repo_root or repo_root in output.parents:
        raise ValueError('Proprietary-derived shader metadata must be private')
    info=json.loads(manifest.read_text(encoding='utf-8'))
    entries=info.get('shader_containers')
    if info.get('bank_version')!=1 or info.get('platform')!='Xbox360' or not isinstance(entries,list) or not 0<len(entries)<=16384:
        raise ValueError('Missing original verified ShaderEffects manifest')
    found=set();records=[];stats={name:0 for name in MATRICES}
    for row in entries:
        ident=row.get('sha256')
        stage=row.get('stage')
        if (not isinstance(ident,str) or len(ident)!=64 or
                any(c not in '0123456789abcdef' for c in ident) or
                stage not in ('VS','PS') or ident in found):
            raise ValueError('Invalid original shader source identity')
        found.add(ident)
        original=(manifest.parent/'Xenos'/(ident+'.xenos.bin')).read_bytes()
        if len(original)!=row.get('bytes') or hashlib.sha256(original).hexdigest()!=ident:
            raise ValueError('Original Xenos reflection source mismatch')
        spans=inspect_container(original,stage)
        stats={name:stats[name]+int(binding!=ABSENT) for name,binding in zip(MATRICES,spans)}
        entry=bytearray(bytes.fromhex(ident)+struct.pack('<I',1 if stage=='VS' else 2))
        for index,size in spans:entry+=struct.pack('<II',index,size)
        assert len(entry)==68
        records.append((ident,entry))
    records.sort()
    payload=bytearray(MAGIC+struct.pack('<II',len(records),1))
    for _,record in records: payload.extend(record)
    output.parent.mkdir(parents=True,exist_ok=True)
    tmp=None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent,prefix='.hulk_ref_',
                                         delete=False) as f:
            tmp=f.name;f.write(payload);f.flush();os.fsync(f.fileno())
        os.replace(tmp,output)
    finally:
        if tmp:Path(tmp).unlink(missing_ok=True)
    return {'format':'HEORREF1','shaders':len(records),
            'original_matrix_bindings':stats,
            'camera_abi_verified':False,
            'sha256':hashlib.sha256(payload).hexdigest()}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,required=True)
    p.add_argument('--private-output',type=Path,required=True)
    args=p.parse_args()
    print(json.dumps(generate(args.manifest,args.private_output,
                              Path(__file__).resolve().parents[1]),sort_keys=True))

if __name__=='__main__': main()
