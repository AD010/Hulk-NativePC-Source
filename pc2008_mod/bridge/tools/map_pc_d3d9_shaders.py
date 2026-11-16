#!/usr/bin/env python3
"""Map the original Hulk PC 2008 D3D9 shaders to exact runtime HLSL override IDs.

No copyrighted bytecode is exported: only names, hashes, sizes, offsets.
Raw Xbox Xenos programs are NEVER accepted as D3D9 shader replacements.
"""
from pathlib import Path
import argparse, hashlib, json, struct
from build_hulk_pc_bridge import hash256,parse_index,SHA256_PC_INDEX,parse_arc_footer

def fnv32(b:bytes):
    value=2166136261
    for x in b:value=((value^x)*16777619)&0xffffffff
    return value

def crc32(b:bytes):
    import zlib
    return zlib.crc32(b)&0xffffffff

def pc_shader_programs(index:bytes,archive:bytes):
    if hash256(index)!=SHA256_PC_INDEX:raise ValueError('Not the validated original PC 2008 index')
    records=dict(parse_index(index))['ShaderEffects']
    parse_arc_footer(archive,records) # Require original verified ARC directory too.
    result=[]
    for key,(at,length,reserved) in sorted(records.items()):
        if reserved or at<4 or at+length>len(archive):raise ValueError('Corrupt original D3D9 effect extent')
        chunk=archive[at:at+length]
        name=chunk[:chunk.find(b'\0')].decode('ascii')
        vcounts={'VS':0,'PS':0}
        found=[]
        for pos in range(8,len(chunk)-16):
            if chunk[pos:pos+4]!=b'CTAB':continue
            start=pos-8
            if start%4 not in (0,1,2,3):raise ValueError('Impossible CTAB alignment')
            version,comment=struct.unpack_from('<II',chunk,start)
            if comment&0xffff!=0xfffe or (comment>>16)<7:
                raise ValueError('Unexpected CTAB location')
            stage={0xfffe0200:'VS',0xffff0200:'PS',0xfffe0300:'VS',0xffff0300:'PS'}.get(version)
            if not stage:raise ValueError('Unsupported PC D3D9 shader model')
            end=None
            for offset in range(pos+4,len(chunk)-3,4):
                if struct.unpack_from('<I',chunk,offset)[0]==0xffff:
                    end=offset+4;break
            if end is None:raise ValueError('Missing D3D9 shader bytecode END instruction')
            binary=chunk[start:end]
            ordinal=vcounts[stage]
            vcounts[stage]+=1
            found.append({'effect_hash':f'{key:08X}','effect_name':name,
                          'stage':stage,'stage_ordinal':ordinal,
                          'pc_shader_model':'2_0' if version&0xffff==0x200 else '3_0',
                          'original_shader_bytes':len(binary),
                          'original_bytecode_sha256':hash256(binary),
                          'override_hlsl_filename':f'{stage}_{fnv32(binary):08X}_{crc32(binary):08X}_{len(binary):08X}.hlsl'})
        if found and vcounts['VS']!=vcounts['PS']:
            raise ValueError('Original PC ShaderEffect missing VS or PS')
        result.extend(found)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pc-index',required=True,type=Path)
    p.add_argument('--pc-shaderef',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    a=p.parse_args()
    payload=pc_shader_programs(a.pc_index.read_bytes(),a.pc_shaderef.read_bytes())
    result={'platform':'Original Hulk PC 2008 D3D9','shader_count':len(payload),
            'shader_stages':{'VS':sum(p['stage']=='VS' for p in payload),
                             'PS':sum(p['stage']=='PS' for p in payload)},
            'runtime_hlsl_validated':False,'shaders':payload}
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(f'Mapped {len(payload)} original PC shader programs; no original bytes exported')
if __name__=='__main__':main()
