#!/usr/bin/env python3
"""Private, read-only USA Hulk TexStreams verifier for supplied .7z.001/.002.
No original game bytes are printed or published. Requires local 7z/7zz/7za.
"""
import argparse, collections, hashlib, json, pathlib, shutil, struct, subprocess, sys, tempfile, zlib
from audit_hulk_tu1_resource_index import parse_index, EXPECTED_US_INDEX_SHA

MAX_REC=64*1024*1024
def usa_index(path):
    content=path.read_bytes()
    digest=hashlib.sha256(content).hexdigest()
    if digest!=EXPECTED_US_INDEX_SHA:
        raise ValueError("Wrong region: INDEX.IND is not the SHA-pinned USA version: "+digest)
    index=parse_index(content)
    if "Textures" not in index or "TexStreams" not in index:
        raise ValueError("Original USA texture groups absent")
    return index

def original_hash(name):
    name=name.upper()
    if name.startswith(("LFXTUI_","TXFLUI_")):name=name[4:]
    if not name or len(name)>128 or any(ord(c)<33 or ord(c)>126 or c in "/\\" for c in name):
        raise ValueError("Invalid source EoR name")
    return zlib.crc32(name.encode("ascii")) & 0xffffffff

def original_link(payload,original_id,stream_index):
    if not payload.startswith(b"LFXT"):raise ValueError("Invalid Xbox LFXT magic")
    end=payload.find(b"\0",4,min(len(payload),133))
    if end<5:raise ValueError("Invalid original LFXT name")
    try:name=payload[4:end].decode("ascii")
    except UnicodeDecodeError as e:raise ValueError("Invalid LFXT ASCII") from e
    if original_hash(name)!=original_id:raise ValueError("EoR texture hash mismatch")
    key=f"{original_hash(name+'_STREAM'):08X}"
    if key not in stream_index:return None
    offsets=[r for r in (34,22) if end+1+r+4<=len(payload)
             and struct.unpack_from(">I",payload,end+1+r)[0]==int(key,16)]
    if len(offsets)!=1:raise ValueError("Ambiguous/missing genuine EoR stream link")
    return key,offsets[0]

def check_span(row,length):
    start,n,extra=row["offset"],row["size"],row["unknown"]
    if start<4 or n<1 or n>MAX_REC or extra or start+n>length:
        raise ValueError("ARC resource/index does not match original source")
    return start,n

def inspect(index,texture_arc,stream_arc):
    textures=index["Textures"]["entries"]
    streams=index["TexStreams"]["entries"]
    total_tex=texture_arc.stat().st_size
    total_stream=stream_arc.stat().st_size
    links=set();off=collections.Counter()
    with texture_arc.open("rb") as source:
        for hashed,row in textures.items():
            start,n=check_span(row,total_tex)
            source.seek(start)
            content=source.read(n)
            if len(content)!=n:raise ValueError("Truncated original Textures.ARC")
            match=original_link(content,int(hashed,16),streams)
            if match:links.add(match[0]);off[match[1]]+=1
    if links!=set(streams):raise ValueError("USA original texture-stream coverage incomplete")
    prefixes=collections.Counter();max_end=0
    with stream_arc.open("rb") as source:
        header=source.read(4)
        if len(header)!=4:raise ValueError("Truncated TexStreams.ARC")
        for row in streams.values():
            start,n=check_span(row,total_stream)
            max_end=max(max_end,start+n)
            source.seek(start)
            prefixes[source.read(min(4,n)).hex()]+=1
    return dict(usa_index_sha256=EXPECTED_US_INDEX_SHA,
                textures=len(textures),streams=len(streams),linked=len(links),
                stream_field_offsets=dict(sorted(off.items())),
                archive_bytes=total_stream,max_indexed_end=max_end,
                archive_header_u32le=int.from_bytes(header,"little"),
                archive_size_header_matches=max_end==int.from_bytes(header,"little"),
                first_four_byte_groups=prefixes.most_common(12),
                xbox_gpu_format_decoded=False,mips_decoded=False,
                region_of_archive_proven_by_index_alone=False)

def parts(first):
    if not first.name.lower().endswith(".7z.001"):
        raise ValueError("Select original TEXSTREA.7z.001")
    second=first.with_name(first.name[:-3]+"002")
    if not first.is_file() or not second.is_file():
        raise FileNotFoundError("Both original TEXSTREA.7z volumes required")
    if not first.stat().st_size or not second.stat().st_size:
        raise ValueError("Original volume is empty")
    return first,second

def extract(first,dest):
    first,_=parts(first)
    exe=next((x for n in ("7z","7zz","7za") if (x:=shutil.which(n))),None)
    if exe is None:raise RuntimeError("Local 7-Zip executable unavailable")
    proc=subprocess.run([exe,"x","-y","-bd",f"-o{dest}",str(first)],
                        stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                        text=True,errors="replace",timeout=1200)
    if proc.returncode:raise RuntimeError("7z multipart extraction failed integrity check")
    found=[p for p in dest.rglob("*") if p.is_file() and p.suffix.lower()==".arc"
           and p.stem.upper().startswith(("TEXSTREA","TEXSTREAM"))]
    if len(found)!=1:raise ValueError("Expected exactly one source TEXSTREA.ARC")
    return found[0]

def sha256(path):
    h=hashlib.sha256()
    with path.open("rb") as stream:
        while block:=stream.read(1024*1024):h.update(block)
    return h.hexdigest()

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--usa-index",required=True,type=pathlib.Path)
    ap.add_argument("--textures-arc",required=True,type=pathlib.Path)
    ap.add_argument("--first-part",required=True,type=pathlib.Path)
    ap.add_argument("--report",type=pathlib.Path)
    a=ap.parse_args()
    ix=usa_index(a.usa_index);first,second=parts(a.first_part)
    with tempfile.TemporaryDirectory(prefix="hulk_us_texstreams_") as t:
        texstreams=extract(first,pathlib.Path(t))
        summary=inspect(ix,a.textures_arc,texstreams)
        summary["part_filenames"]=[first.name,second.name]
        summary["part_sha256"]=[sha256(first),sha256(second)]
        summary["extracted_texstreams_sha256"]=sha256(texstreams)
    report=json.dumps(summary,sort_keys=True,indent=2)+"\n"
    if a.report:a.report.write_text(report,encoding="utf-8")
    else:print(report,end="")
if __name__=="__main__":
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as err:
        print("HULK/USA TexStreams audit failed: "+str(err),file=sys.stderr)
        sys.exit(2)
