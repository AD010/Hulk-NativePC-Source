#!/usr/bin/env python3
"""Source-only original 2008 PC D3D9 vs Xbox Xenos SHADEREF effect crosswalk.

Reads private owner archives and outputs ONLY SHA-256/length/marker counts.
Never mixes platform bytecode or writes original effects to the public repo.
"""
from pathlib import Path
import argparse
import hashlib
import json
from audit_pc_x360_effects import parse_index

def crosswalk(pc_ind:bytes,xb_ind:bytes,pc_arc:bytes,xb_arc:bytes):
    p_order,pc=parse_index(pc_ind);x_order,xb=parse_index(xb_ind)
    if p_order!='PC_LE' or x_order!='XBOX360_BE':
        raise ValueError('Wrong original EoR platform endianness')
    a=pc.get('ShaderEffects',{});b=xb.get('ShaderEffects',{})
    shared=sorted(a.keys()&b.keys())
    records=[]
    for key in shared:
        x,y=a[key],b[key]
        for desc,data in ((x,pc_arc),(y,xb_arc)):
            offset,size,reserved=desc
            if reserved or offset<4 or not size or offset>len(data) or size>len(data)-offset:
                raise ValueError('Invalid original ShaderEffects record')
        pc_payload=pc_arc[x[0]:x[0]+x[1]]
        xb_payload=xb_arc[y[0]:y[0]+y[1]]
        ctab=pc_payload.count(b'CTAB')
        xenos=xb_payload.count(b'\x10\x2a\x11')
        records.append({
            'effect_hash':f'{key:08X}',
            'pc_bytes':len(pc_payload),'xbox_bytes':len(xb_payload),
            'pc_d3d9_ctab':ctab,'xbox_xenos_container_signatures':xenos,
            'pc_sha256':hashlib.sha256(pc_payload).hexdigest(),
            'xbox_sha256':hashlib.sha256(xb_payload).hexdigest(),
            'candidate_shader_count_matches':bool(ctab>0 and ctab==xenos),
            'bytecode_interchangeable':False
        })
    return {'shared_effect_count':len(shared),'shared_effects':records,
            'pc_d3d9_vs_xbox_xenos_runtime_compatible':False,
            'description':'Matching EoR effect hash/marker count is not proof of material/pass equivalence.'}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('pc-index','xbox-index','pc-effects','xbox-effects'):
        p.add_argument('--'+key,required=True,type=Path)
    args=p.parse_args()
    result=crosswalk(args.pc_index.read_bytes(),args.xbox_index.read_bytes(),
                     args.pc_effects.read_bytes(),args.xbox_effects.read_bytes())
    print(json.dumps(result,indent=2,sort_keys=True))
if __name__=='__main__':main()
