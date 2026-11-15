#!/usr/bin/env python3
"""Synthetic original EoR material catalog; never embed private game shaders."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from hulk_eor_effect_ledger import generate

class NativeEffectLedgerTests(unittest.TestCase):
    def test_original_effect_pairing_atomic_and_private(self):
        with tempfile.TemporaryDirectory() as folder:
            parent=Path(folder)
            source=parent/'public'
            source.mkdir()
            game=parent/'private'
            (game/'Xenos').mkdir(parents=True)
            entries=[]
            for effect,stage,payload in ((123,'VS',b'VS-xenos-program'*3),
                                         (123,'PS',b'PS-xenos-program'*3),
                                         (456,'VS',b'another VS-program'*3),
                                         (456,'PS',b'another PS-program'*3)):
                digest=hashlib.sha256(payload).hexdigest()
                (game/'Xenos'/(digest+'.xenos.bin')).write_bytes(payload)
                entries.append({'effect':effect,'stage':stage,
                                'sha256':digest,'bytes':len(payload)})
            manifest=game/'manifest.json'
            manifest.write_text(json.dumps({'bank_version':1,
                'platform':'Xbox360','shader_containers':entries}))
            output=game/'HEORMAT1.bin'
            result=generate(manifest,output,source)
            data=output.read_bytes()
            self.assertEqual(result['original_shaders'],4)
            self.assertEqual(result['original_effects'],2)
            self.assertFalse(result['original_draw_binding_verified'])
            self.assertEqual(data[:8],b'HEORMAT1')
            self.assertEqual(struct.unpack_from('<II',data,8),(4,1))
            self.assertEqual(len(data),16+40*4)
            self.assertEqual(hashlib.sha256(data).hexdigest(),result['sha256'])
            existing=data
            # Missing/changed original bytes must NOT destroy previous catalog.
            (game/'Xenos'/(entries[2]['sha256']+'.xenos.bin')).write_bytes(b'invalid')
            with self.assertRaises(ValueError):generate(manifest,output,source)
            self.assertEqual(existing,output.read_bytes())
            # Do not allow proprietary metadata output into public source repo.
            with self.assertRaises(ValueError):
                generate(manifest,source/'HEORMAT1.bin',source)
            self.assertEqual(existing,output.read_bytes())

if __name__=='__main__':
    unittest.main()
