#!/usr/bin/env python3
"""Privately associate REAL original EoR ShaderEffects with original VS/PS SHA256.

No shader bytes are embedded. This ledger does not infer vertex layout,
textures, blend/depth state or the engine's per-draw material pointer.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile

MAGIC=b'HEORMAT1'
def generate(manifest: Path, destination: Path, source_root: Path):
    manifest=manifest.resolve()
    destination=destination.resolve()
    source_root=source_root.resolve()
    if destination == source_root or source_root in destination.parents:
        raise ValueError('Private original material metadata cannot be published into Git')
    info=json.loads(manifest.read_text(encoding='utf-8'))
    records=info.get('shader_containers')
    if (info.get('bank_version')!=1 or info.get('platform')!='Xbox360'
            or not isinstance(records,list) or not 0<len(records)<=16384):
        raise ValueError('Unverified original EoR ShaderEffects manifest')
    gathered=[]
    stage_map={}
    seen=set()
    for r in records:
        effect,stage,digest=r['effect'],r['stage'],r['sha256']
        if (not isinstance(effect,int) or effect<=0 or effect>0xffffffff
                or stage not in ('VS','PS') or
                not isinstance(digest,str) or len(digest)!=64 or
                any(c not in '0123456789abcdef' for c in digest) or digest in seen):
            raise ValueError('Duplicate/invalid original effect/shader identity')
        seen.add(digest)
        data=(manifest.parent/'Xenos'/(digest+'.xenos.bin')).read_bytes()
        if len(data)!=r['bytes'] or hashlib.sha256(data).hexdigest()!=digest:
            raise ValueError('Original private shader bytes do not match ledger input')
        st=1 if stage=='VS' else 2
        gathered.append((effect,st,bytes.fromhex(digest)))
        stage_map.setdefault(effect,set()).add(st)
    if any(stages!={1,2} for stages in stage_map.values()):
        raise ValueError('Original effect is missing shader stage')
    gathered.sort()
    payload=bytearray(MAGIC+struct.pack('<II',len(gathered),1))
    for effect,st,digest in gathered:
        payload+=struct.pack('<II',effect,st)+digest
    destination.parent.mkdir(parents=True,exist_ok=True)
    tmp=None
    try:
        with tempfile.NamedTemporaryFile(prefix='.eor_effect_',
                dir=destination.parent,delete=False) as f:
            tmp=f.name
            f.write(payload)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp,destination)
    finally:
        if tmp:
            Path(tmp).unlink(missing_ok=True)
    return {'original_shaders':len(gathered),'original_effects':len(stage_map),
            'metadata_bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest(),
            'original_draw_binding_verified':False}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,required=True)
    p.add_argument('--private-output',type=Path,required=True)
    a=p.parse_args()
    print(json.dumps(generate(a.manifest,a.private_output,
                              Path(__file__).resolve().parents[1]),sort_keys=True))
if __name__=='__main__':
    main()
