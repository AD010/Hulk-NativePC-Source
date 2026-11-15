#!/usr/bin/env python3
"""Owner-indexed FMOD pair and Windows DLL ABI audit. Read-only, no audio/code assets exported."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

USA_INDEX_SHA256 = "ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d"


def dll_arch(data):
    if len(data)<256 or len(data)>(32<<20) or data[:2]!=b"MZ":
        raise ValueError("Invalid FMOD PE binary")
    pe=struct.unpack_from("<I",data,0x3c)[0]
    if pe<64 or pe+24>len(data) or data[pe:pe+4]!=b"PE\x00\x00":
        raise ValueError("Invalid PE signature")
    machine=struct.unpack_from("<H",data,pe+4)[0]
    return {"machine":f"{machine:04X}",
            "arch":{0x14c:"x86",0x8664:"x64"}.get(machine,"unknown"),
            "upx_container":b"UPX0" in data or b"UPX1" in data,
            "sha256":hashlib.sha256(data).hexdigest(),
            "bytes":len(data)}


def compare_rows(rows,a,b):
    if len(a)!=len(b) or len(a)>(64<<20) or not rows:
        raise ValueError("Invalid archive pair")
    ranges=[];previous=4;equal=0
    for row in sorted(rows,key=lambda r:r["offset"]):
        off,size=row["offset"],row["size"]
        if row["reserved"] or size<20 or off<previous or off+size>len(a):
            raise ValueError("Invalid FMOD record bounds or overlap")
        equal+=a[off:off+size]==b[off:off+size]
        ranges.append((off,off+size))
        previous=off+size
    differing=[i for i,(x,y) in enumerate(zip(a,b)) if x!=y]
    in_record=0
    j=0
    for lo,hi in ranges:
        while j<len(differing) and differing[j]<lo:j+=1
        k=j
        while k<len(differing) and differing[k]<hi:k+=1
        in_record+=k-j;j=k
    return {"indexed_records":len(rows),"identical_indexed_records":equal,
            "different_indexed_records":len(rows)-equal,
            "total_changed_bytes":len(differing),
            "changed_bytes_inside_indexed_records":in_record,
            "changed_bytes_outside_indexed_records":len(differing)-in_record,
            "first_changed_offset":min(differing) if differing else None,
            "last_changed_offset":max(differing) if differing else None,
            "indexed_end":previous,"unindexed_tail_length":len(a)-previous,
            "unindexed_tail_semantics_verified":False}


def audit(index,a,b,event,ex):
    from audit_hulk_pc_x360_ui_assets import parse_index
    digest=hashlib.sha256(index).hexdigest()
    if digest!=USA_INDEX_SHA256:
        raise ValueError("Wrong user-pinned Xbox USA index(2).ind, refuse old index")
    decoded=parse_index(index)
    if decoded["endianness"]!="big":
        raise ValueError("Expected big-endian Xbox 360 index")
    rows=decoded["groups"]["FmodProjects"]
    ev,core=dll_arch(event),dll_arch(ex)
    result=compare_rows(rows,a,b)
    return {"schema":"hulk_fmod_owner_pair_v1","pinned_index_sha256":digest,
            "archive_a_sha256":hashlib.sha256(a).hexdigest(),
            "archive_b_sha256":hashlib.sha256(b).hexdigest(),
            "archive_comparison":result,
            "fmod_event":ev,"fmodex":core,
            "original_dlls_usable_in_windows_x64_process":
                ev["arch"]=="x64" and core["arch"]=="x64",
            "full_native_audio_implemented":False,
            "original_audio_files_embedded_in_report":False}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ("index","archive_a","archive_b","fmod_event","fmodex"):
        p.add_argument("--"+name.replace("_","-"),type=Path,required=True)
    p.add_argument("--output",type=Path)
    a=p.parse_args()
    result=audit(*(getattr(a,n).read_bytes() for n in
        ("index","archive_a","archive_b","fmod_event","fmodex")))
    report=json.dumps(result,indent=2)+"\n"
    if a.output:a.output.write_text(report,encoding="utf-8")
    print(report)

if __name__=="__main__":main()
