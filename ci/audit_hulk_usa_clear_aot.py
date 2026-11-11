#!/usr/bin/env python3
"""Offline proof of Hulk USA XDK Clear/ClearF from a pinned user-owned AOT.

Inputs: user's prior AOT EXE and Stage1 guest_host_mapping.csv.
Never prints or uploads guest code, shader bytes or binary contents.
"""
import argparse
import bisect
import csv
import hashlib
import json
from pathlib import Path
import struct

AOT_SHA256 = "687a0f58f0b7283bb96774d2c541020b2d286ab83d247aecad6c2ba5d6b23257"
# AOT host entries validated by reverse mapping (99,269 functions).
ADDRESSES = {
    0x82888D68: 0x1410ECD70,  # Clear packed D3DCOLOR wrapper
    0x82888C40: 0x141FEC5A0,  # ClearF float4 / rectangle wrapper
    0x828885B0: 0x1424CEB80,  # inner GPU clear; NOT a hook
    0x8288A900: 0x14247BF90,  # previously verified BeginTiling
}


def read_mapping(path: Path) -> dict[int, int]:
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = csv.DictReader(stream)
        if not {"guest_address", "native_entry"} <= set(rows.fieldnames or []):
            raise ValueError("Missing guest/native mapping columns")
        output = {}
        for row in rows:
            guest, native = int(row["guest_address"], 16), int(row["native_entry"], 16)
            if guest in output:
                raise ValueError("Duplicate guest mapping address")
            output[guest] = native
    if len(output) < 99000 or not all(output.get(g) == n for g, n in ADDRESSES.items()):
        raise ValueError("Not the 99,269-function Hulk USA AOT map")
    return output


def read_text_section(exe: bytes) -> tuple[int, bytes]:
    if exe[:2] != b"MZ" or len(exe) < 0x100:
        raise ValueError("Not an AOT PE32+ binary")
    pe = struct.unpack_from("<I", exe, 0x3C)[0]
    if pe + 0x18 > len(exe) or exe[pe:pe+4] != b"PE\x00\x00":
        raise ValueError("Invalid AOT PE signature")
    count = struct.unpack_from("<H", exe, pe+6)[0]
    opt_size = struct.unpack_from("<H", exe, pe+20)[0]
    opt = pe+24
    if opt+opt_size > len(exe) or struct.unpack_from("<H",exe,opt)[0] != 0x20B:
        raise ValueError("Not a valid 64-bit PE")
    base = struct.unpack_from("<Q",exe,opt+24)[0]
    table = opt+opt_size
    for i in range(count):
        pos = table+i*40
        if pos+40 > len(exe):
            raise ValueError("Truncated section table")
        name = exe[pos:pos+8].split(b"\0",1)[0]
        vsize, vaddr, rsize, offset = struct.unpack_from("<IIII",exe,pos+8)
        if name == b".text":
            if not vsize or offset+rsize > len(exe):
                raise ValueError("Corrupt AOT .text")
            return base+vaddr,exe[offset:offset+min(vsize,rsize)]
    raise ValueError("AOT .text section missing")


def direct_calls(code: bytes, start: int, dest: int) -> list[int]:
    return [start+i for i in range(len(code)-4)
            if code[i] == 0xE8 and
            start+i+5+struct.unpack_from("<i",code,i+1)[0] == dest]


def verify(aot: Path, mapping_csv: Path) -> dict:
    exe = aot.read_bytes()
    digest = hashlib.sha256(exe).hexdigest()
    if digest != AOT_SHA256:
        raise ValueError("AOT SHA-256 mismatch; cannot assume identical hook ABI")
    mapping = read_mapping(mapping_csv)
    start, code = read_text_section(exe)
    hosts = sorted(mapping.values())

    def function(guest):
        address = mapping[guest]
        i = bisect.bisect_right(hosts,address)
        if i >= len(hosts):
            raise ValueError("Missing AOT function boundary")
        end = hosts[i]
        if not start <= address < end <= start+len(code):
            raise ValueError("AOT function outside .text")
        return address,code[address-start:end-start]

    def edge(src,dst):
        addr,body = function(src)
        return [hex(site) for site in direct_calls(body,addr,mapping[dst])]

    _,clear = function(0x82888D68)
    _,clearf = function(0x82888C40)
    _,inner = function(0x828885B0)
    graph = {
        "Clear_to_ClearF":edge(0x82888D68,0x82888C40),
        "ClearF_to_inner_clear":edge(0x82888C40,0x828885B0),
        "BeginTiling_to_ClearF":edge(0x8288A900,0x82888C40),
    }
    if [len(sites) for sites in graph.values()] != [2,1,1]:
        raise ValueError("USA Clear/ClearF AOT call graph drift")
    checks = {
        "Clear_reads_packed_r7": b"\x8b\x4f\x38" in clear,
        "Clear_normalizes_color_to_float4": b"\x0f\x59\x87\x10\x03\x00\x00" in clear,
        "ClearF_handles_null_rect_r5": b"\x83\x7e\x28\x00" in clearf,
        "inner_clear_reads_Z_f1": b"\xf2\x0f\x10\x87\x58\x01\x00\x00" in inner,
    }
    if not all(checks.values()):
        raise ValueError("USA clear packed-color, rect or Z ABI drift")
    return {
        "AOT_sha256":digest,
        "mapped_functions":len(mapping),
        "Clear":"0x82888D68",
        "ClearF":"0x82888C40",
        "inner_not_hook":"0x828885B0",
        "call_sites":graph,
        "ABI_checks":checks,
        "status":"static_AOT_verification_not_Vulkan_runtime_proof",
    }


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("aot",type=Path)
    p.add_argument("mapping",type=Path)
    p.add_argument("--output",type=Path)
    args=p.parse_args()
    result=json.dumps(verify(args.aot,args.mapping),indent=2)+"\n"
    if args.output:
        args.output.write_text(result,encoding="utf-8")
    print(result)


if __name__=="__main__":
    main()
