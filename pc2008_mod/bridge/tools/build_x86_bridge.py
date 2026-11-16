#!/usr/bin/env python3
"""Build freestanding Win32/x86 HulkBridge.asi with clang-cl and lld-link."""
from pathlib import Path
import hashlib, struct, shutil, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'src'

def pe_check(path:Path):
    b=path.read_bytes(); o=struct.unpack_from('<I',b,0x3c)[0]
    if b[:2]!=b'MZ' or b[o:o+4]!=b'PE\0\0' or struct.unpack_from('<H',b,o+4)[0]!=0x14c or struct.unpack_from('<H',b,o+24)[0]!=0x10b or not struct.unpack_from('<H',b,o+22)[0]&0x2000:
        raise ValueError('Not a Win32 i386 PE32 DLL')
    return hashlib.sha256(b).hexdigest()

def import_fix(lib):
    d=bytearray(lib.read_bytes());p=8;n=0
    if not d.startswith(b'!<arch>\n'): raise ValueError('Not an import library')
    while p+60<=len(d):
        length=int(d[p+48:p+58].decode().strip());it=p+60
        if it+length>len(d):raise ValueError('Truncated COFF import archive')
        if d[it:it+4]==b'\0\0\xff\xff':
            flags=struct.unpack_from('<H',d,it+18)[0]
            if (flags>>2)&7!=2:raise ValueError('Unknown import alias encoding')
            struct.pack_into('<H',d,it+18,(flags&~(7<<2))|(3<<2));n+=1
        p=it+length+(length&1)
    if p!=len(d) or n<15:raise ValueError('COFF imports malformed')
    lib.write_bytes(d)

def build():
    clang=shutil.which('clang-cl');lld=shutil.which('lld-link')
    if not clang or not lld:raise RuntimeError('clang-cl/lld-link missing')
    (ROOT/'bin').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        tmp=Path(tmp); lib=tmp/'kernel32.lib';o=tmp/'bridge.obj';os=tmp/'shader.obj';op=tmp/'proxy.obj'
        subprocess.run([lld,'/lib','/def:'+str(SRC/'kernel32.def'),'/machine:x86','/out:'+str(lib)],check=True)
        import_fix(lib)
        subprocess.run([clang,'-target','i686-pc-windows-msvc','/nologo','/c','/O2','/GS-','/Zl','/W4',str(SRC/'HulkBridge.c'),'/Fo:'+str(o)],check=True)
        subprocess.run([clang,'-target','i686-pc-windows-msvc','/nologo','/c','/O2','/GS-','/Zl','/W4',str(SRC/'HulkShader.c'),'/Fo:'+str(os)],check=True)
        subprocess.run([clang,'-target','i686-pc-windows-msvc','/nologo','/c','/O2','/GS-','/Zl','/W4',str(SRC/'dinput8_proxy_bridge.c'),'/Fo:'+str(op)],check=True)
        output=ROOT/'bin'/'HulkBridge.asi'
        subprocess.run([lld,'/dll','/machine:x86','/entry:DllMain@12','/nodefaultlib',str(o),str(lib),'/out:'+str(output)],check=True)
        shader_output=ROOT/'bin'/'HulkShader.asi'
        subprocess.run([lld,'/dll','/machine:x86','/entry:DllMain@12','/nodefaultlib',str(os),str(lib),'/out:'+str(shader_output)],check=True)
        proxy_output=ROOT/'bin'/'dinput8.dll'
        subprocess.run([lld,'/dll','/machine:x86','/entry:DllMain@12','/nodefaultlib','/def:'+str(SRC/'dinput8.def'),str(op),str(lib),'/out:'+str(proxy_output)],check=True)
    digest=pe_check(output)
    print('Built',output.name,output.stat().st_size,'bytes; SHA256',digest)
    print('Built HulkShader.asi SHA256',pe_check(ROOT/'bin'/'HulkShader.asi'))
    print('Built dinput8.dll SHA256',pe_check(ROOT/'bin'/'dinput8.dll'))
    print('Windows game-runtime behavior is NOT verified.')
if __name__=='__main__':build()
