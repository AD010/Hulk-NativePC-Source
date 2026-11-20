#!/usr/bin/env python3
"""Exact Xenia Edge/Xenos raw shader -> original Hulk EoR pass crosswalk.

Read user-owned SHADEREF.ARC and private derived HEORMAT2.bin. Output contains
only SHA-256 identifiers and pass indices, NEVER game shader bytes. A raw
microcode match is not a live Draw/material owner; an ambiguous pair is marked
ambiguous rather than guessed. No runtime Xenia/PM4 dependency is introduced.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct

MAGIC = b'HEORMAT2'
MAX_ARCHIVE = 64 << 20
MAX_DUMP = 1 << 20


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def original_shaders(archive: bytes) -> dict[str, tuple[str, str]]:
    if not 36 <= len(archive) <= MAX_ARCHIVE:
        raise ValueError('Original ShaderEffects archive size invalid')
    result = {}
    offset = 0
    while True:
        at = archive.find(b'\x10\x2a\x11', offset)
        if at < 0:
            break
        offset = at + 1
        if len(archive)-at < 36:
            raise ValueError('Truncated original Xenos shader container')
        (flags, virtual, physical, _reserved, constants, definition,
         shader_offset, zero_a, zero_b) = struct.unpack_from('>9I',archive,at)
        if flags & 0xffffff00 != 0x102a1100:
            continue
        if (virtual < 36 or not physical or physical % 4 or
            virtual + physical > len(archive)-at or
            not 36 <= constants < virtual or
            not 36 <= shader_offset < virtual or
            (definition and not 36 <= definition < virtual) or
            zero_a or zero_b):
            raise ValueError('Malformed original Xenos shader container')
        blob = archive[at:at+virtual+physical]
        stage = 'VS' if flags & 1 else 'PS'
        source = digest(blob)
        physical_sha = digest(blob[virtual:])
        if source in result and result[source] != (stage,physical_sha):
            raise ValueError('One source SHA maps to incompatible original shaders')
        result[source] = (stage,physical_sha)
        offset = at + virtual + physical
    if not result:
        raise ValueError('No original Xenos containers found in archive')
    return result


def load_pairs(raw: bytes, shaders: dict[str,tuple[str,str]]) -> list[dict]:
    if len(raw)<16 or raw[:8] != MAGIC:
        raise ValueError('Expected actual HEORMAT2 original pass ledger')
    count,version=struct.unpack_from('<II',raw,8)
    if version!=2 or not 0<count<=16384 or len(raw)!=16+72*count:
        raise ValueError('Original VS/PS pass ledger size or version invalid')
    result=[]
    seen=set()
    for i in range(count):
        at=16+72*i
        effect,slot=struct.unpack_from('<II',raw,at)
        vs,ps=raw[at+8:at+40].hex(),raw[at+40:at+72].hex()
        if not effect or (vs,ps) in seen or vs not in shaders or ps not in shaders:
            raise ValueError('Missing, duplicated or source-mismatched original shader pair')
        seen.add((vs,ps))
        vstage,vcode=shaders[vs]
        pstage,pcode=shaders[ps]
        if vstage!='VS' or pstage!='PS':
            raise ValueError('Original VS/PS source stage mismatch')
        result.append({'effect_hash':f'{effect:08x}', 'pass_index':slot,
                       'vertex_source_sha256':vs, 'pixel_source_sha256':ps,
                       'vertex_ucode_sha256':vcode, 'pixel_ucode_sha256':pcode})
    grouped={}
    for p in result:
        key=(p['vertex_ucode_sha256'],p['pixel_ucode_sha256'])
        grouped[key]=grouped.get(key,0)+1
    for p in result:
        key=(p['vertex_ucode_sha256'],p['pixel_ucode_sha256'])
        p['unambiguous_ucode_pair']=grouped[key]==1
    return result


def inspect_dump_dir(directory: Path,shaders:dict[str,tuple[str,str]]) -> list[dict]:
    lookup={}
    for source,(stage,ucode) in shaders.items():
        lookup.setdefault((stage,ucode),set()).add(source)
    result=[]
    files=sorted(p for p in directory.rglob('*') if p.is_file())
    for p in files:
        filename=p.name.lower()
        stage=('VS' if filename.endswith('.bin.vs') else
               'PS' if filename.endswith(('.bin.fs','.bin.ps')) else None)
        if stage is None:
            continue
        data=p.read_bytes()
        if not 4<=len(data)<=MAX_DUMP or len(data)%4:
            raise ValueError('Invalid original Xenia shader microcode dump size')
        swap=b''.join(data[i:i+4][::-1] for i in range(0,len(data),4))
        matches=[]
        for kind,raw in (('exact',data),('word_swapped',swap)):
            source=lookup.get((stage,digest(raw)),set())
            if source:
                matches.append((kind,source))
        if len(matches)!=1:
            result.append({'filename':p.name,'stage':stage,'status':'ambiguous' if matches else 'unmatched'})
            continue
        mode,source=matches[0]
        result.append({'filename':p.name,'stage':stage,'status':'matched',
                       'byte_order':mode,'original_shader_sha256':sorted(source),
                       'unique_source_shader':len(source)==1})
    return result


def generate(archive:Path,ledger:Path,output:Path,*,dump_dir:Path|None=None,
             source_root:Path|None=None) -> dict:
    out=output.resolve()
    if source_root is not None:
        repo=source_root.resolve()
        if out==repo or repo in out.parents:
            raise ValueError('Derived private shader identifiers cannot be saved in public source tree')
    shaders=original_shaders(archive.read_bytes())
    pairs=load_pairs(ledger.read_bytes(),shaders)
    dumps=inspect_dump_dir(dump_dir,shaders) if dump_dir is not None else []
    report={'format':'HULK_XENIA_EOR_SHADER_CROSSWALK_V1',
            'shader_sources':len(shaders),'original_effect_passes':len(pairs),
            'uniquely_matched_ucode_pairs':sum(x['unambiguous_ucode_pair'] for x in pairs),
            'ambiguous_ucode_pairs':sum(not x['unambiguous_ucode_pair'] for x in pairs),
            'original_pass_map':pairs,'xenia_dump_matches':dumps}
    out.parent.mkdir(parents=True,exist_ok=True)
    temp=out.with_name(out.name+'.tmp')
    temp.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    temp.replace(out)
    return report


def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbox-effects',required=True,type=Path)
    parser.add_argument('--heormat2',required=True,type=Path)
    parser.add_argument('--xenia-dumps',type=Path)
    parser.add_argument('--private-report',required=True,type=Path)
    args=parser.parse_args()
    result=generate(args.xbox_effects,args.heormat2,args.private_report,
                    dump_dir=args.xenia_dumps,source_root=Path(__file__).resolve().parents[1])
    print(json.dumps({k:result[k] for k in ('shader_sources','original_effect_passes',
          'uniquely_matched_ucode_pairs','ambiguous_ucode_pairs')}))


if __name__=='__main__':
    main()
