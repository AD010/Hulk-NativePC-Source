#!/usr/bin/env python3
"""Rebuild the Hulk Win32 DirectInput proxy + native D3D9 smoke probe.

Requires clang-cl and lld-link, but no Windows SDK, VC++ redistributable
or MinGW runtime. This is a read-only graphics API probe, not a remaster.
"""
from pathlib import Path
import hashlib
import shutil
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parent

def run(*cmd):
    subprocess.run([str(x) for x in cmd],check=True)

def fix_import_name_types(path):
    """Preserve stdcall COFF symbols, import undecorated real Win32 exports."""
    data=bytearray(path.read_bytes())
    if not data.startswith(b'!<arch>\n'):
        raise ValueError('Unexpected COFF import library')
    cursor,count=8,0
    while cursor+60<=len(data):
        length=int(bytes(data[cursor+48:cursor+58]).decode('ascii').strip())
        item=cursor+60
        if item+length>len(data):raise ValueError('Truncated COFF archive')
        if data[item:item+4]==b'\x00\x00\xff\xff':
            flags=struct.unpack_from('<H',data,item+18)[0]
            if ((flags>>2)&7)!=2:raise ValueError('Unexpected import name type')
            struct.pack_into('<H',data,item+18,(flags&~(7<<2))|(3<<2))
            count+=1
        cursor=item+length+(length&1)
    if count<15 or cursor!=len(data):
        raise ValueError('Incomplete Win32 API import table')
    path.write_bytes(data)

def check_pe(path):
    b=path.read_bytes()
    if b[:2]!=b'MZ' or len(b)<256:raise ValueError('Not Windows PE')
    pe=struct.unpack_from('<I',b,0x3c)[0]
    if (b[pe:pe+4]!=b'PE\0\0' or
            struct.unpack_from('<H',b,pe+4)[0]!=0x14c or
            struct.unpack_from('<H',b,pe+24)[0]!=0x10b or
            not struct.unpack_from('<H',b,pe+22)[0]&0x2000):
        raise ValueError('Output must be x86 PE32 DLL')
    return hashlib.sha256(b).hexdigest()

def main():
    clang=shutil.which('clang-cl')
    lld=shutil.which('lld-link')
    if not clang or not lld:
        raise SystemExit('Need LLVM clang-cl and lld-link')
    out=ROOT/'bin';out.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='hulk_pc_asi_x86_') as temp:
        temp=Path(temp)
        lib=temp/'kernel32.lib'
        run(lld,'/lib','/def:'+str(ROOT/'kernel32.def'),
            '/machine:x86','/out:'+str(lib))
        fix_import_name_types(lib)
        for stem in ('dinput8_proxy','HulkPCProbe'):
            run(clang,'-target','i686-pc-windows-msvc','/nologo',
                '/c','/O2','/GS-','/Zl','/W4',str(ROOT/(stem+'.c')),
                '/Fo:'+str(temp/(stem+'.obj')))
        run(lld,'/dll','/machine:x86','/entry:DllMain@12','/nodefaultlib',
            '/def:'+str(ROOT/'dinput8.def'),str(temp/'dinput8_proxy.obj'),
            str(lib),'/out:'+str(out/'dinput8.dll'))
        run(lld,'/dll','/machine:x86','/entry:DllMain@12','/nodefaultlib',
            str(temp/'HulkPCProbe.obj'),str(lib),
            '/out:'+str(out/'HulkPCProbe.asi'))
    for name in ('dinput8.dll','HulkPCProbe.asi'):
        print(name,'SHA256',check_pe(out/name))
    print('PE and import architecture validated; no Windows gameplay test.')

if __name__=='__main__':main()
