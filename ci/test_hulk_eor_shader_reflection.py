#!/usr/bin/env python3
"""Source-only test of verified original ShaderContainer matrix reflection."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from hulk_eor_shader_reflection import generate, inspect_container, ABSENT

def fake(stage: str):
    blob=bytearray(196)
    flags=0x102a1101 if stage=='VS' else 0x102a1100
    struct.pack_into('>9I',blob,0,flags,160,36,0,36,0,120,0,0)
    # Constant table begins at byte 40; records are at offset 68.
    struct.pack_into('>7I',blob,40,80,0,0,2,28,0,0)
    struct.pack_into('>IHHHHII',blob,68,70,2,11,4,0,0,0)
    struct.pack_into('>IHHHHII',blob,88,80,2,7,4,0,0,0)
    blob[110:119]=b'ViewProj\0'
    blob[120:126]=b'World\0'
    return bytes(blob)

class EorShaderReflectionTests(unittest.TestCase):
    def test_reflected_original_matrix_slots(self):
        self.assertEqual(inspect_container(fake('VS'),'VS'),
                         ((11,4),(7,4),ABSENT,ABSENT))
        self.assertEqual(inspect_container(fake('PS'),'PS'),
                         (ABSENT,ABSENT,ABSENT,ABSENT))
        with self.assertRaises(ValueError):
            inspect_container(fake('VS'),'PS')
        with self.assertRaises(ValueError):
            inspect_container(fake('VS')[:-1],'VS')
    def test_atomic_private_catalog(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);src=root/'source';src.mkdir()
            dest=root/'private';(dest/'Xenos').mkdir(parents=True)
            inputs=[]
            for stage in ('VS','PS'):
                blob=fake(stage)
                key=hashlib.sha256(blob).hexdigest()
                (dest/'Xenos'/(key+'.xenos.bin')).write_bytes(blob)
                inputs.append({'stage':stage,'bytes':len(blob),'sha256':key})
            manifest=dest/'manifest.json'
            manifest.write_text(json.dumps({'bank_version':1,'platform':'Xbox360',
                                            'shader_containers':inputs}))
            file=dest/'HEORREF1.bin'
            result=generate(manifest,file,src)
            data=file.read_bytes()
            self.assertEqual(data[:8],b'HEORREF1')
            self.assertEqual(struct.unpack_from('<II',data,8),(2,1))
            self.assertEqual(len(data),16+68*2)
            self.assertEqual(result['original_matrix_bindings']['ViewProj'],1)
            self.assertFalse(result['camera_abi_verified'])
            saved=data
            (dest/'Xenos'/(inputs[0]['sha256']+'.xenos.bin')).write_bytes(b'corrupted')
            with self.assertRaises(ValueError):generate(manifest,file,src)
            self.assertEqual(file.read_bytes(),saved)
            with self.assertRaises(ValueError):generate(manifest,src/'cache.bin',src)

if __name__=='__main__':unittest.main()
