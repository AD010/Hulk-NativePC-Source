#!/usr/bin/env python3
"""Build a SAFE virtual original-PC EoR index + one ARC from PC-compatible resources.

No Xbox payload endian swapping or replacement without format validation.
Only the exact original 2008 PC index/ARC can be input. Original files untouched.
The output is a separate game-relative HulkBridge/ directory for optional ASI.
"""
from __future__ import annotations
import argparse, hashlib, json, os, re, struct, tempfile
from pathlib import Path

SHA256_PC_INDEX = '3309ac7650c53c7bf64f797f962f6c5961c33774718347a2a0999fc04b04a33a'
# File basename to original PC index group, allow only formats we validate.
CATEGORIES = {'ShaderEffects': 'SHADEREF.ARC', 'Models': 'MODELS.ARC'}
HEADER = b'HULKBRIDGE-PC-v1\n'


def hash256(data): return hashlib.sha256(data).hexdigest()


def parse_index(data: bytes):
    if not 24 <= len(data) <= (8<<20): raise ValueError('Bad PC index size')
    count = struct.unpack_from('<I',data,0)[0]
    if count != 52: raise ValueError('Unexpected original PC category count')
    offsets = list(struct.unpack_from('<%dI' % (count+1),data,4))
    if (offsets[0] != 4+4*(count+1) or offsets[-1] != len(data)-4 or
        any(x>=y for x,y in zip(offsets,offsets[1:])) or
        struct.unpack_from('<I',data,len(data)-4)[0] != 1000):
        raise ValueError('Corrupt original PC group offsets/footer')
    groups=[]
    for j in range(0,count,2):
        a,b,c=offsets[j:j+3]
        name=data[a:b].split(b'\0',1)[0].decode('ascii')
        if not name or '\\' in name or '/' in name:raise ValueError('Unsafe PC group')
        n=struct.unpack_from('<I',data,b)[0]
        if c-b != 4+16*n:raise ValueError('Invalid PC resource table')
        ids=struct.unpack_from('<%dI'%n,data,b+4) if n else ()
        if any(ids[k] >= ids[k+1] for k in range(n-1)):
            raise ValueError('Invalid PC resource ordering')
        props={key:struct.unpack_from('<III',data,b+4+n*4+12*i)
               for i,key in enumerate(ids)}
        groups.append((name,props))
    return groups


def pack_index(groups):
    count=len(groups)*2
    if count!=52: raise ValueError('Unrecognized PC category structure')
    output=bytearray(4+4*(count+1)); offsets=[]
    for name,entries in groups:
        offsets.append(len(output))
        raw=name.encode('ascii')+b'\x00'
        output.extend(raw)
        output.extend(b'\x00'*((-len(raw))%4))
        offsets.append(len(output))
        ids=sorted(entries)
        output.extend(struct.pack('<I',len(ids)))
        for h in ids:output.extend(struct.pack('<I',h))
        for h in ids:output.extend(struct.pack('<III',*entries[h]))
    offsets.append(len(output));output.extend(struct.pack('<I',1000))
    struct.pack_into('<I',output,0,count)
    struct.pack_into('<%dI'%(count+1),output,4,*offsets)
    return bytes(output)


def crc_hash(name):
    import zlib
    return zlib.crc32(name.upper().encode('ascii')) & 0xffffffff


def validate_model(payload:bytes,resource_id:int):
    if len(payload)<18 or payload[:6]!=b'\x01\x00\x00\x00\x00\x00':
        raise ValueError('Not a PC EoR Models record (Xbox endianness?)')
    terminator=payload.find(b'\0',6,min(len(payload),256))
    if terminator<7 or terminator+6>len(payload):raise ValueError('Invalid original model name')
    name=payload[6:terminator].decode('ascii')
    if not re.fullmatch(r'[A-Za-z0-9_\-. ]+',name):raise ValueError('Invalid original model name characters')
    if payload[terminator+1] not in (0,3):raise ValueError('Unsupported original EoR model kind')
    n=struct.unpack_from('<I',payload,terminator+2)[0]
    if n>1024:raise ValueError('Suspicious original model surface count')
    if n and not b'\x00' in payload[terminator+6:terminator+260]:
        raise ValueError('Missing first original PC material name')
    if resource_id!=crc_hash(name) and (resource_id!=0x21e3a96c or name!='hulk_front-end_model'):
        raise ValueError('Model resource ID does not match original model name')


def validate_effect(payload:bytes, resource_id:int):
    if not 160<=len(payload)<(1<<20):raise ValueError('Invalid D3D9 ShaderEffect length')
    # Shader code must be PC D3D9 with actual CTAB reflection, not Xenos code.
    end=payload.find(b'\0',0,128)
    if end<1:raise ValueError('Original PC ShaderEffect has no name')
    name=payload[:end].decode('ascii')
    if resource_id!=crc_hash(name):raise ValueError('Effect ID does not match original PC name')
    offsets=[i for i in range(len(payload)) if payload.startswith(b'CTAB',i)]
    if len(offsets)<2 or len(offsets)%2 or len(offsets)>256:
        raise ValueError('Original PC ShaderEffect lacks D3D9 VS/PS pairs')
    vs=ps=0
    for at in offsets:
        if at<8:raise ValueError('D3D9 CTAB lacks shader version/comment')
        version,comment=struct.unpack_from('<II',payload,at-8)
        if (comment&0xffff)!=0xfffe or (comment>>16)<7:
            raise ValueError('CTAB is not a D3D9 shader comment token')
        if version in (0xfffe0200,0xfffe0300):vs+=1
        elif version in (0xffff0200,0xffff0300):ps+=1
        else:raise ValueError('CTAB is not preceded by a PC VS/PS shader')
    if vs!=ps:raise ValueError('PC ShaderEffect has unpaired VS/PS binaries')
    if payload[:1] in (b'\0',b'\xff') or b'\x10\x2a\x11' in payload:
        raise ValueError('Xbox Xenos ShaderEffect data not accepted in PC index')



def parse_arc_footer(arc:bytes, entries:dict):
    if len(arc)<8 or len(arc)>512*1024*1024:
        raise ValueError('Invalid PC EoR ARC size')
    directory=struct.unpack_from('<I',arc,0)[0]
    if directory<4 or directory+4>len(arc):
        raise ValueError('Original PC ARC directory offset is invalid')
    count=struct.unpack_from('<I',arc,directory)[0]
    if count!=len(entries):raise ValueError('ARC footer and PC index entry counts disagree')
    at=directory+4
    result={}
    previous=-1
    for i in range(count):
        if at+12>len(arc):raise ValueError('Truncated EoR ARC entry')
        key,offset,size=struct.unpack_from('<III',arc,at)
        at+=12
        end=arc.find(b'\0',at,min(len(arc),at+240))
        if end<at+1:raise ValueError('Malformed original ARC resource name')
        name=arc[at:end]
        at=end+1
        if at+8>len(arc):raise ValueError('Truncated EoR ARC resource timestamp')
        timestamp=arc[at:at+8]
        at+=8
        if key<=previous or entries.get(key)!=(offset,size,0) or not name:
            raise ValueError('ARC directory is inconsistent with PC index')
        previous=key
        result[key]=(name,timestamp)
    if any(arc[at:]):raise ValueError('Unknown nonzero PC ARC trailing metadata')
    # Original Windows EoR ARC is padded to a 32-byte boundary.
    if len(arc)%32 or len(arc)-at>=32:raise ValueError('Unexpected PC ARC alignment')
    return directory,result

def pack_arc_footer(entries:dict, names:dict, base_offset:int):
    data=bytearray(struct.pack('<I',len(entries)))
    for key in sorted(entries):
        offset,size,reserved=entries[key]
        if reserved or key not in names:raise ValueError('Unverified EoR archive directory record')
        name,timestamp=names[key]
        if not name or len(name)>240 or b'\0' in name or len(timestamp)!=8:
            raise ValueError('Invalid original resource name / timestamp')
        data+=struct.pack('<III',key,offset,size)+name+b'\0'+timestamp
    data+=b'\0'*(-(base_offset+len(data))%32)
    return data

def build(base_index:Path,base_arc:Path,manifest:Path,output_dir:Path):
    raw_index=base_index.read_bytes()
    if hash256(raw_index)!=SHA256_PC_INDEX:
        raise ValueError('Base index is not the verified original 2008 PC index')
    groups=parse_index(raw_index)
    info=json.loads(manifest.read_text(encoding='utf-8'))
    if info.get('format')!='HulkBridgePC-v1' or info.get('platform')!='PC_D3D9':
        raise ValueError('Must provide converted PC-target resources, NOT Xbox ARC')
    selected=info.get('category')
    if selected not in CATEGORIES:raise ValueError('No PC resource format validator for '+str(selected))
    mapping=dict(groups)
    original=mapping[selected]
    arc=base_arc.read_bytes()
    if len(arc)>512*1024*1024 or len(arc)<4:
        raise ValueError('Unsafe PC ARC size')
    if any(not(4<=off<=len(arc) and size<=len(arc)-off and not reserved)
           for off,size,reserved in original.values()):
        raise ValueError('Original ARC incompatible with PC index category')
    directory,names=parse_arc_footer(arc,original)
    # Verify ARC internal directory roundtrip BEFORE creating any files.
    original_repacked=pack_arc_footer(original,names,directory)
    if original_repacked != arc[directory:]:
        raise ValueError('Original ARC resource directory encoding drift')
    if output_dir.exists() and any(output_dir.iterdir()):
        raise ValueError('Refusing to overwrite an existing virtual bridge package')
    substitutions=info.get('records')
    if not isinstance(substitutions,list) or not 1<=len(substitutions)<=1000:
        raise ValueError('No explicit converted PC records supplied')
    new=dict(original)
    new_arc=bytearray(arc[:directory])
    original_bytes=len(arc)
    changes=[]
    seen=set()
    for r in substitutions:
        value=r.get('resource_hash')
        key=int(value,0) if isinstance(value,str) else value
        if not isinstance(key,int) or not 0<key<=0xffffffff or key in seen:
            raise ValueError('Duplicate/invalid target resource hash')
        seen.add(key)
        path=Path(r['pc_payload_path'])
        if not path.is_absolute():path=(manifest.parent/path).resolve()
        src=path.read_bytes()
        if not 0<len(src)<16*1024*1024:raise ValueError('Unsafe converted PC payload length')
        if hash256(src)!=r.get('sha256'):
            raise ValueError('PC payload SHA-256 does not match supplied manifest')
        (validate_model(src,key) if selected=='Models' else validate_effect(src,key))
        # No in-place mutation of original bytes; every changed ID points to
        # appended PC-compatible bytes in the virtual ARC.
        pos=len(new_arc)
        new_arc.extend(src)
        if key not in names:
            nend=src.find(b'\0',6 if selected=='Models' else 0)
            name=src[(6 if selected=='Models' else 0):nend]
            names[key]=(name,b'\0'*8)
        else:
            # Replacements must preserve original source resource name.
            nend=src.find(b'\0',6 if selected=='Models' else 0)
            incoming=src[(6 if selected=='Models' else 0):nend]
            if incoming!=names[key][0]:
                raise ValueError('Replacement PC resource name mismatch')
        new[key]=(pos,len(src),0)
        changes.append({'resource_hash':f'0x{key:08X}',
                        'kind':'override' if key in original else 'extra',
                        'payload_bytes':len(src),'sha256':hash256(src)})
    new_directory_at=len(new_arc)
    new_arc.extend(pack_arc_footer(new,names,new_directory_at))
    struct.pack_into('<I',new_arc,0,new_directory_at)
    modified=[(name,new if name==selected else records) for name,records in groups]
    new_index=pack_index(modified)
    again=parse_index(new_index)
    if [(x,len(y)) for x,y in again]!=[(x,len(y)) for x,y in modified]:
        raise ValueError('Virtual PC index roundtrip mismatch')
    # Original records in other groups stay untouched. Keep original ARC's
    # byte sequence intact except its required 4-byte total-length header.
    if new_arc[4:directory]!=arc[4:directory]:
        raise ValueError('Source PC archive data unexpectedly modified')
    output_dir.mkdir(parents=True,exist_ok=False)
    (output_dir/'data').mkdir()
    (output_dir/'index.ind').write_bytes(new_index)
    (output_dir/'data'/CATEGORIES[selected]).write_bytes(new_arc)
    # Plugin whitelist is deliberately short and cannot include arbitrary paths.
    (output_dir/'HulkBridge.map').write_bytes(
        HEADER+('index.ind\n'+CATEGORIES[selected]+'\n').encode('ascii'))
    report={'category':selected,'archive':CATEGORIES[selected],
            'records':changes,'original_index_sha256':hash256(raw_index),
            'virtual_index_sha256':hash256(new_index),'virtual_arc_sha256':hash256(new_arc),
            'original_archive_bytes':original_bytes,
            'virtual_archive_bytes':len(new_arc),'original_index_entries':len(original),
            'virtual_index_entries':len(new),'converted_pc_content_only':True,
            'live_pc_loader_verified':False}
    (output_dir/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('pc-index','pc-archive','manifest','output-dir'):
        p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    print(json.dumps(build(a.pc_index,a.pc_archive,a.manifest,a.output_dir),indent=2))
if __name__=='__main__':main()