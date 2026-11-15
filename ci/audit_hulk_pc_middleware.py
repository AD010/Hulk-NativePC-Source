#!/usr/bin/env python3
"""Read-only original 2008 PC Hulk PE DLL imports and Havok type-name evidence.

Does not copy the original executable or its bytes into a repository.
PC middleware is a cross-reference only; USA XEX calls are not inferred.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import struct

MAX_EXE = 128 << 20
KEYWORD = re.compile(rb'(?<![A-Za-z0-9_])(hk[pab][A-Z][A-Za-z0-9_]{3,90})(?![A-Za-z0-9_])')


def read_imports(data: bytes) -> list[str]:
    def u16(p: int) -> int:
        if p < 0 or p + 2 > len(data):
            raise ValueError('Truncated PE16 header')
        return struct.unpack_from('<H', data, p)[0]
    def u32(p: int) -> int:
        if p < 0 or p + 4 > len(data):
            raise ValueError('Truncated PE32 header')
        return struct.unpack_from('<I', data, p)[0]
    if len(data) < 256 or data[:2] != b'MZ':
        raise ValueError('Not DOS/PE executable')
    pe = u32(0x3c)
    if pe < 64 or pe + 24 > len(data) or data[pe:pe+4] != b'PE\0\0':
        raise ValueError('Missing PE signature')
    coff = pe + 4
    sections = u16(coff + 2)
    optsize = u16(coff + 16)
    if not 1 <= sections <= 96 or optsize < 112 or coff+20+optsize+sections*40 > len(data):
        raise ValueError('Invalid PE sections')
    opt = coff + 20
    magic = u16(opt)
    if magic not in (0x10b, 0x20b):
        raise ValueError('Unsupported PE optional header')
    directory_offset = opt + (96 if magic == 0x10b else 112)
    if directory_offset + 16 > opt + optsize:
        raise ValueError('Missing import directory')
    import_rva, import_size = u32(directory_offset + 8), u32(directory_offset + 12)
    if not import_rva or not import_size:
        return []
    shdr = opt + optsize
    layout = []
    for i in range(sections):
        p = shdr + i*40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from('<4I', data, p+8)
        layout.append((virtual_address, max(virtual_size,raw_size), raw_offset, raw_size))
    def to_offset(rva: int, width: int) -> int:
        for base, length, offset, raw in layout:
            if base <= rva and rva-base+width <= length and rva-base+width <= raw:
                pos = offset+rva-base
                if pos+width <= len(data):
                    return pos
        raise ValueError('PE import RVA outside file-backed section')
    if import_size > 100000:
        raise ValueError('Unreasonably large PE import table')
    out = []
    for index in range(min(2048, import_size//20+2)):
        descriptor = to_offset(import_rva + index*20, 20)
        fields = struct.unpack_from('<5I', data, descriptor)
        if fields == (0, 0, 0, 0, 0):
            break
        name = to_offset(fields[3], 1)
        end = data.find(b'\0', name, min(len(data), name+256))
        if end < 0:
            raise ValueError('Unterminated import DLL name')
        value = data[name:end].decode('ascii').lower()
        if not re.fullmatch(r'[a-z0-9._+-]{1,255}\.dll', value):
            raise ValueError('Invalid DLL import name')
        out.append(value)
    else:
        raise ValueError('Unterminated PE import descriptor table')
    return sorted(set(out))


def audit(data: bytes) -> dict:
    if len(data) > MAX_EXE:
        raise ValueError('Original PE unusually large')
    imports = read_imports(data)
    names = set(m.group(1).decode('ascii') for m in KEYWORD.finditer(data))
    return {'schema':'hulk_original_pc_middleware_v1',
            'pe_import_dlls':imports,
            'native_fmod_event_import': 'fmod_event.dll' in imports,
            'native_fmodex_import': 'fmodex.dll' in imports,
            'havok_pc_type_name_examples':sorted(x for x in names
                 if x.startswith(('hkpRigidBody','hkpWorld','hkpConstraint','hkpRagdoll')))[:20],
            'havok_pc_symbol_count':len(names),
            'xbox_usa_backend_verified':False,
            'note':'PC PE imports/type strings only; no proof of USA Xbox 360 guest ABI, address or job scheduler.'}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('original_pc_exe',type=Path)
    p.add_argument('--output',type=Path)
    args=p.parse_args()
    report=audit(args.original_pc_exe.read_bytes())
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    main()
