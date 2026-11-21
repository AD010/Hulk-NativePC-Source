#!/usr/bin/env python3
"""Original USA Xenos VIRTUAL-header SHA256 -> original full-source identity.

Generated private HEORVRT1 map resolves 5,194 original program identities
without storing GPU microcode, private guest pointers, PDB bytes or game
assets in public source. Runtime must separately authenticate that the
hash input is a FULL virtual header of the currently bound original
XDK shader, never merely an adjacent source file or orphaned allocation.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile

MAGIC=b"HEORVRT1"
VERSION=1
MAX_SHADERS=16384
MAX_VIRTUAL_BYTES=8<<20
ENTRY=struct.Struct("<32s32sII")


def extract(manifest:Path,output:Path,public_repo:Path)->dict:
    manifest=manifest.resolve()
    output=output.resolve()
    public_repo=public_repo.resolve()
    if output==public_repo or public_repo in output.parents:
        raise ValueError("The private Xenos virtual fingerprint cache must not be published")
    obj=json.loads(manifest.read_text(encoding="utf-8"))
    if obj.get("bank_version")!=1 or obj.get("platform")!="Xbox360":
        raise ValueError("Wrong original Xbox shader source bank")
    rows=obj.get("shader_containers")
    if not isinstance(rows,list) or not 0<len(rows)<=MAX_SHADERS:
        raise ValueError("Invalid original source shader index")
    seen_full={}
    seen_virtual={}
    for row in rows:
        digest=row.get("sha256")
        stage=row.get("stage")
        if (not isinstance(digest,str) or len(digest)!=64 or
            any(c not in "0123456789abcdef" for c in digest) or
            stage not in ("VS","PS")):
            raise ValueError("Malformed original ShaderEffects source record")
        full=(manifest.parent/"Xenos"/(digest+".xenos.bin")).read_bytes()
        if len(full)!=row.get("bytes") or hashlib.sha256(full).hexdigest()!=digest:
            raise ValueError("Original ShaderEffects source identity mismatch")
        flag,virtual,physical=struct.unpack_from(">III",full)
        if ((flag&0xffffff00)!=0x102a1100 or
            ("VS" if flag&1 else "PS")!=stage or virtual<36 or
            virtual>MAX_VIRTUAL_BYTES or physical<=0 or
            virtual+physical!=len(full)):
            raise ValueError("Malformed original Xenos virtual shader header")
        vhash=hashlib.sha256(full[:virtual]).digest()
        full_sha=bytes.fromhex(digest)
        entry=(vhash,full_sha,1 if stage=="VS" else 2,virtual)
        if full_sha in seen_full:
            if seen_full[full_sha]!=entry:
                raise ValueError("One original full shader changed its virtual source")
            continue
        if vhash in seen_virtual:
            raise ValueError("Ambiguous virtual source header among original shaders")
        seen_full[full_sha]=entry
        seen_virtual[vhash]=full_sha
    entries=sorted(seen_full.values())
    if len(entries)!=len(seen_virtual):
        raise ValueError("Original shader virtual fingerprint collision")
    payload=bytearray(MAGIC+struct.pack("<II",len(entries),VERSION))
    for entry in entries:
        payload+=ENTRY.pack(*entry)
    output.parent.mkdir(parents=True,exist_ok=True)
    scratch=None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent,
                                         prefix=".hulk_virtual_",
                                         delete=False) as f:
            scratch=f.name
            f.write(payload)
            f.flush()
            os.fsync(f.fileno())
        os.replace(scratch,output)
    finally:
        if scratch:
            Path(scratch).unlink(missing_ok=True)
    return {
        "format":"HEORVRT1",
        "source_original_full_shader_count":len(entries),
        "unique_original_virtual_shader_headers":len(seen_virtual),
        "ambiguous_original_virtual_shader_headers":0,
        "highest_original_virtual_shader_header_bytes":
            max(x[3] for x in entries),
        "private_virtual_to_original_sha256_bank_sha256":
            hashlib.sha256(payload).hexdigest(),
        "runtime_bound_xdk_shader_object_header_offset_verified":False,
        "runtime_full_material_and_texture_ownership_verified":False,
        "no_original_guest_or_shader_bytes_published":True,
    }


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--private-manifest",type=Path,required=True)
    p.add_argument("--private-output",type=Path,required=True)
    args=p.parse_args()
    print(json.dumps(extract(args.private_manifest,args.private_output,
                             Path(__file__).resolve().parents[1]),
                     sort_keys=True))


if __name__=="__main__":
    main()
