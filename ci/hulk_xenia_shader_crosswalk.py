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
from pathlib import Path, PurePosixPath
import stat
import struct
import zipfile

MAGIC = b'HEORMAT2'
MAX_ARCHIVE = 64 << 20
MAX_DUMP = 1 << 20
MAX_ZIP_ENTRIES = 120000
MAX_TOTAL_DUMP_BYTES = 512 << 20


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


def _shader_dump_stage(name: str) -> str | None:
    lower=name.lower()
    if lower.endswith('.bin.vs'):
        return 'VS'
    if lower.endswith(('.bin.fs','.bin.ps')):
        return 'PS'
    return None


def _dump_files(source: Path):
    """Read only original raw GPU microcode, never trust ZIP paths or sizes."""
    if source.is_dir():
        total=0
        count=0
        for path in sorted(source.rglob('*')):
            if path.is_symlink():
                raise ValueError('Symlinked shader dump is unsafe')
            if not path.is_file() or _shader_dump_stage(path.name) is None:
                continue
            count+=1
            if count>MAX_ZIP_ENTRIES:
                raise ValueError('Too many original shader dump files')
            size=path.stat().st_size
            if not 4<=size<=MAX_DUMP or size%4:
                raise ValueError('Invalid original Xenia shader microcode dump size')
            total+=size
            if total>MAX_TOTAL_DUMP_BYTES:
                raise ValueError('Original shader dumps exceed input budget')
            yield path.relative_to(source).as_posix(),path.read_bytes()
        return
    if not source.is_file() or not zipfile.is_zipfile(source):
        raise ValueError('Xenia shader input must be a directory or ZIP archive')
    with zipfile.ZipFile(source,'r') as archive:
        entries=archive.infolist()
        if len(entries)>MAX_ZIP_ENTRIES:
            raise ValueError('Too many files in Xenia shader archive')
        total=0
        seen=set()
        for info in entries:
            raw_name=info.filename.replace('\\','/')
            name=PurePosixPath(raw_name)
            if (name.is_absolute() or ':' in raw_name or
                any(part in ('.','..') for part in raw_name.split('/'))):
                raise ValueError('Unsafe Xenia shader ZIP path')
            if info.is_dir():
                continue
            if (info.external_attr >> 16) & 0o170000 == stat.S_IFLNK:
                raise ValueError('Symlinked ZIP shader dump is unsafe')
            relative=name.as_posix()
            if relative in seen:
                raise ValueError('Duplicate Xenia shader ZIP member')
            seen.add(relative)
            if _shader_dump_stage(relative) is None:
                continue
            size=info.file_size
            if not 4<=size<=MAX_DUMP or size%4:
                raise ValueError('Invalid original Xenia shader microcode dump size')
            total+=size
            if total>MAX_TOTAL_DUMP_BYTES:
                raise ValueError('Original shader dumps exceed input budget')
            with archive.open(info) as stream:
                data=stream.read(MAX_DUMP+1)
                if len(data)!=size:
                    raise ValueError('Truncated Xenia shader ZIP member')
                yield relative,data


def inspect_dump_dir(directory: Path,shaders:dict[str,tuple[str,str]]) -> list[dict]:
    lookup={}
    for source,(stage,ucode) in shaders.items():
        lookup.setdefault((stage,ucode),set()).add(source)
    result=[]
    for relative,data in _dump_files(directory):
        stage=_shader_dump_stage(relative)
        swap=b''.join(data[i:i+4][::-1] for i in range(0,len(data),4))
        matches={}
        for kind,raw in (('exact',data),('word_swapped',swap)):
            found=lookup.get((stage,digest(raw)),set())
            for shader in found:
                matches.setdefault(shader,set()).add(kind)
        if not matches:
            result.append({'filename':relative,'stage':stage,'status':'unmatched'})
            continue
        modes=set().union(*matches.values())
        # If data and byte-swapped data both match distinct original source
        # programs, the byte-order identity itself is ambiguous.
        if len(modes)!=1:
            result.append({'filename':relative,'stage':stage,'status':'ambiguous',
                           'original_shader_sha256':sorted(matches)})
            continue
        result.append({'filename':relative,'stage':stage,'status':'matched',
                       'byte_order':next(iter(modes)),
                       'original_shader_sha256':sorted(matches),
                       'unique_source_shader':len(matches)==1})
    return result


def shader_dump_report(dumps: list[dict],pairs: list[dict]) -> dict:
    """Passes with both raw shader stages observed are *candidates* only."""
    matched=[entry for entry in dumps if entry['status']=='matched']
    captured={shader for entry in matched
              for shader in entry['original_shader_sha256']}
    candidates=[{'effect_hash':p['effect_hash'],
                 'pass_index':p['pass_index'],
                 'unambiguous_ucode_pair':p['unambiguous_ucode_pair']}
                for p in pairs if p['vertex_source_sha256'] in captured
                and p['pixel_source_sha256'] in captured]
    return {'raw_dump_files':len(dumps),
            'matched_files':len(matched),
            'unmatched_files':sum(x['status']=='unmatched' for x in dumps),
            'ambiguous_files':sum(x['status']=='ambiguous' for x in dumps),
            'unique_matched_shader_sources':len(captured),
            'both_stages_seen_pass_candidates':candidates,
            'candidates_are_live_draw_evidence':False}


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
            'original_pass_map':pairs,'xenia_dump_matches':dumps,
            'dump_coverage':shader_dump_report(dumps,pairs)}
    out.parent.mkdir(parents=True,exist_ok=True)
    temp=out.with_name(out.name+'.tmp')
    temp.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    temp.replace(out)
    return report


def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbox-effects',required=True,type=Path)
    parser.add_argument('--heormat2',required=True,type=Path)
    parser.add_argument('--xenia-dumps',type=Path,
                        help='Directory or ZIP with raw Xenia .bin.vs/.bin.fs')
    parser.add_argument('--private-report',required=True,type=Path)
    args=parser.parse_args()
    result=generate(args.xbox_effects,args.heormat2,args.private_report,
                    dump_dir=args.xenia_dumps,source_root=Path(__file__).resolve().parents[1])
    print(json.dumps({k:result[k] for k in ('shader_sources','original_effect_passes',
          'uniquely_matched_ucode_pairs','ambiguous_ucode_pairs')}))


if __name__=='__main__':
    main()
