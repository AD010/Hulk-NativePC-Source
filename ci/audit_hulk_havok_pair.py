#!/usr/bin/env python3
"""Read-only Hulk 2008 PC/Xbox Havok 5.1 archive/index integrity audit.

No original geometry, Havok class payloads or game executable bytes are
written to the report. The exact supplied Xbox index is pinned by SHA;
no previous PAL/other Xbox index may silently replace it.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
from audit_hulk_pc_x360_ui_assets import parse_index

USER_XBOX_INDEX_SHA = 'ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d'
MAGIC = bytes.fromhex('57e0e05710c0c010')
VERSION = b'Havok-5.1.0-r1\x00'

def audit_archive(path: Path, rows: list[dict], platform: str) -> tuple[dict, set[str]]:
    if platform not in ('PC', 'Xbox360'):
        raise ValueError('Unknown original Havok platform')
    data = path.read_bytes()
    if len(data)<4 or len(data)>(256<<20):
        raise ValueError('Unreasonable Havok archive length')
    order, layout = ('<',b'\x04\x01\x00\x01') if platform=='PC' else ('>',b'\x04\x00\x00\x01')
    previous=4
    hashes=set()
    total=0
    fixup_totals={'local':0,'global':0,'virtual':0}
    for item in sorted(rows,key=lambda r:r['offset']):
        key = item['hash']
        off,size,reserved=item['offset'],item['size'],item['reserved']
        if not (off>=previous and 96<=size<=(4<<20) and
                off+size<=len(data) and reserved==0):
            raise ValueError('Invalid or overlapping indexed original Havok bytes')
        raw=data[off:off+size]
        marker=raw[:256].find(MAGIC)
        if marker<0 or marker+208>len(raw):
            raise ValueError('Missing/truncated original Havok section header')
        header=raw[marker:]
        tag,ver=struct.unpack_from(order+'II',header,8)
        section_count=struct.unpack_from(order+'I',header,20)[0]
        if (tag!=int(key,16) or ver!=5 or section_count!=3 or
                header[16:20]!=layout or VERSION not in header[36:80]):
            raise ValueError('Wrong indexed Havok userTag/version/endianness')
        # Havok 5.1 PC and Xbox both use 48-byte section records at +64.
        # Validate physical byte bounds and fixup offsets, not just magic.
        names=(b'__classnames__',b'__data__',b'__types__')
        prev_end=0
        sections=[]
        for j,name in enumerate(names):
            p=64+48*j
            tag=header[p:p+20]
            # All original section tags end with the 0xFF marker byte.
            if tag!=name+b'\x00'*(19-len(name))+b'\xff':
                raise ValueError('Wrong original Havok section tag or sentinel')
            start,loc,glob,virt,exp,imp,end=struct.unpack_from(order+'7I',header,p+20)
            if (start<208 or start<prev_end or
                    start>len(header) or end>len(header)-start or
                    not (loc<=glob<=virt<=exp<=imp<=end)):
                raise ValueError('Invalid original Havok native fixup section bounds')
            prev_end=start+end
            sections.append((start,loc,glob,virt,exp,imp,end))
        # Validate each original 32-bit relocation edge, not merely the
        # host-visible section offsets. Do not turn guest offsets into x64
        # host pointers until class/shape layout conversion is implemented.
        for section,s in enumerate(sections):
            start,local,glob,virt,exp,_,_=s
            for kind,begin,finish,step in (
                    ('local',local,glob,8),
                    ('global',glob,virt,12),
                    ('virtual',virt,exp,12)):
                for i in range(begin,finish,step):
                    remaining=finish-i
                    if remaining<step:
                        if header[start+i:start+finish]!=b'\xff'*remaining:
                            raise ValueError('Invalid original Havok fixup padding')
                        continue
                    words=struct.unpack_from(order+('II' if step==8 else 'III'),header,start+i)
                    if words[0]==0xffffffff:
                        if any(x!=0xffffffff for x in words):
                            raise ValueError('Broken Havok fixup sentinel')
                        continue
                    source_offset=words[0]
                    dst_section=section if step==8 else words[1]
                    dst_offset=words[1] if step==8 else words[2]
                    if (source_offset+4>local or dst_section>=3 or
                            dst_offset>=sections[dst_section][1]):
                        raise ValueError('Original Havok fixup out of bounds')
                    fixup_totals[kind]+=1
        previous=off+size
        total+=size
        hashes.add(key)
    if struct.unpack_from('<I',data)[0]!=previous:
        raise ValueError('Original EoR archive used length differs from indexed end')
    return ({'record_count':len(rows),'file_bytes':len(data),
             'sha256':hashlib.sha256(data).hexdigest(),
             'indexed_payload_bytes':total,
             'last_indexed_end':previous,
             'padding_tail_bytes':len(data)-previous,
             'havok_version':'Havok-5.1.0-r1',
             'layout_rules':layout.hex(),
             'all_indexed_havok_sections_and_tags_valid':True,
             'validated_fixup_edges':fixup_totals,
             'native_havok_geometry_deserialized':False},hashes)

def audit(pc_index:Path,xbox_index:Path,pc_havoks:Path,pc_mopps:Path,
          xbox_havoks:Path,xbox_mopps:Path, *,
          xbox_index_sha: str = USER_XBOX_INDEX_SHA) -> dict:
    pc_raw,xb_raw=pc_index.read_bytes(),xbox_index.read_bytes()
    digest=hashlib.sha256(xb_raw).hexdigest()
    if digest!=xbox_index_sha:
        raise ValueError('Wrong user-supplied USA index(2).ind: no stale Xbox offsets accepted')
    pc,xb=parse_index(pc_raw),parse_index(xb_raw)
    if pc['endianness']!='little' or xb['endianness']!='big':
        raise ValueError('PC/Xbox Havok index endian mismatch')
    categories={}
    for group,pc_path,xb_path in (
            ('Havoks',pc_havoks,xbox_havoks),
            ('HavokMopps',pc_mopps,xbox_mopps)):
        left,lk=audit_archive(pc_path,pc['groups'][group],'PC')
        right,rk=audit_archive(xb_path,xb['groups'][group],'Xbox360')
        categories[group]={'pc':left,'xbox':right,
                           'shared_ids':len(lk&rk),
                           'pc_only':len(lk-rk),'xbox_only':len(rk-lk)}
    return {'schema':'hulk_pc_xbox_havok_archive_evidence_v1',
            'pc_index_sha256':hashlib.sha256(pc_raw).hexdigest(),
            'user_supplied_xbox_index_sha256':digest,
            'original_havok_assets':categories,
            'totals':{'pc':sum(c['pc']['record_count'] for c in categories.values()),
                      'xbox':sum(c['xbox']['record_count'] for c in categories.values())},
            'usa_region_verified_by_index_alone':False,
            'hkpWorld_timestep_address_verified':False,
            'havok_multicore_job_scheduler_verified':False,
            'native_havok_deserialization_implemented':False,
            'report_contains_private_game_bytes':False}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('pc_index','xbox_index','pc_havoks','pc_mopps',
                 'xbox_havoks','xbox_mopps'):
        p.add_argument('--'+name.replace('_','-'),type=Path,required=True)
    p.add_argument('--output',type=Path)
    a=p.parse_args()
    report=audit(*(getattr(a,name) for name in
        ('pc_index','xbox_index','pc_havoks','pc_mopps','xbox_havoks','xbox_mopps')))
    if a.output:
        a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))
if __name__=='__main__':
    main()
