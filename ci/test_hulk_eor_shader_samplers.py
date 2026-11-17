#!/usr/bin/env python3
"""Real XDK source sampler register identity, without game binary fixtures."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from hulk_eor_shader_samplers import inspect_original_samplers,generate


def fake(stage:str,empty:bool=False)->bytes:
    blob=bytearray(208)
    struct.pack_into('>9I',blob,0,
        0x102a1101 if stage=='VS' else 0x102a1100,
        192,16,0,36,0,168,0,0)
    struct.pack_into('>7I',blob,40,0,0,0,0 if empty else 2,28,0,0)
    if not empty:
        struct.pack_into('>IHHHHII',blob,68,104,3,0,1,0,0,0)
        struct.pack_into('>IHHHHII',blob,88,120,3,1,1,0,0,0)
        blob[144:155]=b'DiffuseMap\0'
        blob[160:170]=b'NormalMap\0'
    return bytes(blob)


class OriginalXdkSamplers(unittest.TestCase):
    def test_original_source_slot_and_stage(self):
        self.assertEqual(inspect_original_samplers(fake('PS'),'PS'),
                         ((0,'DiffuseMap'),(1,'NormalMap')))
        self.assertEqual(inspect_original_samplers(fake('VS',True),'VS'),())
        with self.assertRaises(ValueError):
            inspect_original_samplers(fake('VS'),'PS')
        broken=bytearray(fake('PS'))
        struct.pack_into('>H',broken,88+6,0)
        with self.assertRaisesRegex(ValueError,'Duplicate'):
            inspect_original_samplers(bytes(broken),'PS')
        broken=bytearray(fake('PS'))
        broken[145]=ord('/')
        with self.assertRaisesRegex(ValueError,'Unverified'):
            inspect_original_samplers(bytes(broken),'PS')

    def test_source_sha_private_sidecar_and_sentinel(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            public=root/'public'
            public.mkdir()
            private=root/'private'
            bank=private/'Xenos'
            bank.mkdir(parents=True)
            items=[]
            for stage,empty in [('VS',True),('PS',False)]:
                data=fake(stage,empty)
                sha=hashlib.sha256(data).hexdigest()
                (bank/(sha+'.xenos.bin')).write_bytes(data)
                items.append({'sha256':sha,'stage':stage,'bytes':len(data)})
            manifest=private/'manifest.json'
            def save(entries):
                manifest.write_text(json.dumps({
                    'bank_version':1,'platform':'Xbox360',
                    'shader_containers':entries}))
            save(items)
            dest=private/'HEORSMP1.bin'
            result=generate(manifest,dest,public)
            payload=dest.read_bytes()
            self.assertEqual(result['source_shaders'],2)
            self.assertEqual(result['source_sampler_slots'],2)
            self.assertEqual(result['zero_sampler_shaders'],1)
            self.assertEqual(payload[:8],b'HEORSMP1')
            self.assertEqual(struct.unpack_from('<II',payload,8),(3,1))
            self.assertEqual(len(payload),16+3*72)
            self.assertIn(struct.pack('<II',1,0xffffffff),payload)
            self.assertIn(b'DiffuseMap\0',payload)
            saved=payload
            save(items+[dict(items[0])])
            self.assertEqual(generate(manifest,dest,public)['source_shaders'],2)
            self.assertEqual(dest.read_bytes(),saved)
            conflict=dict(items[0])
            conflict['stage']='PS'
            save(items+[conflict])
            with self.assertRaises(ValueError):generate(manifest,dest,public)
            self.assertEqual(dest.read_bytes(),saved)
            save(items)
            (bank/(items[1]['sha256']+'.xenos.bin')).write_bytes(b'corrupt')
            with self.assertRaises(ValueError):generate(manifest,dest,public)
            self.assertEqual(dest.read_bytes(),saved)
            with self.assertRaises(ValueError):generate(manifest,public/'bad',public)


if __name__=='__main__':
    unittest.main()
