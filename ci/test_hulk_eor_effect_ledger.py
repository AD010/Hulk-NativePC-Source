#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from hulk_eor_effect_ledger import generate

class EorPassLedgerTests(unittest.TestCase):
    def test_actual_stage_group_order_and_atomicity(self):
        with tempfile.TemporaryDirectory() as root:
            base=Path(root); src=base/'source'; src.mkdir()
            priv=base/'private'; (priv/'Xenos').mkdir(parents=True)
            entries=[]
            for effect,stage,offset,payload in [
                    (2,'VS',100,b'A'*48),(2,'VS',200,b'B'*48),
                    (2,'PS',400,b'C'*48),(2,'PS',500,b'D'*48),
                    (3,'VS',10,b'E'*48),(3,'PS',70,b'F'*48)]:
                digest=hashlib.sha256(payload).hexdigest()
                (priv/'Xenos'/(digest+'.xenos.bin')).write_bytes(payload)
                entries.append({'effect':effect,'stage':stage,
                                'relative_offset':offset,'sha256':digest,'bytes':48})
            manifest=priv/'manifest.json'
            def save(rows):
                manifest.write_text(json.dumps({'bank_version':1,'platform':'Xbox360',
                                                'shader_containers':rows}))
            save(entries)
            out=priv/'HEORMAT2.bin'
            result=generate(manifest,out,src)
            data=out.read_bytes()
            self.assertEqual(data[:8],b'HEORMAT2')
            self.assertEqual(struct.unpack_from('<II',data,8),(3,2))
            self.assertEqual(result['ordinal_pair_candidates'],3)
            self.assertFalse(result['all_runtime_passes_verified'])
            self.assertEqual(len(data),16+3*72)
            self.assertEqual(struct.unpack_from('<II',data,16),(2,0))
            self.assertEqual(data[16+8:16+40],bytes.fromhex(entries[0]['sha256']))
            self.assertEqual(data[16+40:16+72],bytes.fromhex(entries[2]['sha256']))
            original=data
            save(entries[:-1])
            with self.assertRaises(ValueError):generate(manifest,out,src)
            self.assertEqual(out.read_bytes(),original)
            save(entries)
            with self.assertRaises(ValueError):generate(manifest,src/'bank',src)
            self.assertEqual(out.read_bytes(),original)
            entries[2]['relative_offset']=50
            save(entries)
            with self.assertRaises(ValueError):generate(manifest,out,src)
            self.assertEqual(out.read_bytes(),original)

if __name__ == '__main__':unittest.main()
