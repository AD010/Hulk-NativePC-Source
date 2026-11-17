#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib
from hulk_eor_effect_ledger import generate, _shader_technique

def SyntheticContainer(effect: str, technique: str, stage: str) -> bytes:
    pdb = f'{effect}_{technique}_SourceProgram_{"vp" if stage=="VS" else "fp"}.updb'
    tail = pdb.encode('ascii') + b'\0'
    virtual = 36 + len(tail)
    return (struct.pack('>9I', 0x102a1101 if stage == 'VS' else 0x102a1100,
                        virtual, 16, 0, 36, 0, 36, 0, 0)
            + tail + bytes(16))


class EorPassLedgerTests(unittest.TestCase):
    def test_real_source_debug_technique_pairing_gate(self):
        effect = 'HulkTestEffect'
        key = zlib.crc32(effect.upper().encode('ascii')) & 0xffffffff
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            public = root / 'public'
            public.mkdir()
            private = root / 'private'
            xenos = private / 'Xenos'
            xenos.mkdir(parents=True)
            original_effect = (effect + '\0').encode() + b'0123456789abcdef'
            (private / f'{key}.effect.bin').write_bytes(original_effect)
            records = [{'hash': key, 'sha256': hashlib.sha256(original_effect).hexdigest(),
                        'private_file': f'{key}.effect.bin',
                        'bytes': len(original_effect)}]
            def shader(stage, technique):
                binary = SyntheticContainer(effect, technique, stage)
                sha = hashlib.sha256(binary).hexdigest()
                (xenos / f'{sha}.xenos.bin').write_bytes(binary)
                return {'effect': key, 'stage': stage, 'relative_offset':
                        100 if stage == 'VS' else 1000,
                        'sha256': sha, 'bytes': len(binary)}
            vs = shader('VS', 'Deferred')
            ps = shader('PS', 'Deferred')
            manifest = private / 'manifest.json'
            out = private / 'HEORMAT2.bin'
            def save(pixel):
                manifest.write_text(json.dumps({
                    'bank_version': 1, 'platform': 'Xbox360',
                    'records': records, 'shader_containers': [vs, pixel]}))
            save(ps)
            result = generate(manifest, out, public)
            assert result['source_pdb_technique_pairs_verified'] == 1
            first = out.read_bytes()
            self.assertEqual(_shader_technique(
                (xenos / f"{vs['sha256']}.xenos.bin").read_bytes(),
                'VS', effect), 'Deferred')
            mismatched = shader('PS', 'ForwardLightmap')
            save(mismatched)
            with self.assertRaisesRegex(ValueError, 'technique names do not match'):
                generate(manifest, out, public)
            self.assertEqual(out.read_bytes(), first)
            # Even correctly paired stages cannot claim another ShaderEffect.
            ps_wrong = shader('PS', 'Deferred')
            wrong_effect = SyntheticContainer('OtherEffect', 'Deferred', 'PS')
            bad_sha = hashlib.sha256(wrong_effect).hexdigest()
            (xenos / f'{bad_sha}.xenos.bin').write_bytes(wrong_effect)
            ps_wrong['sha256'], ps_wrong['bytes'] = bad_sha, len(wrong_effect)
            save(ps_wrong)
            with self.assertRaisesRegex(ValueError, 'different EoR effect'):
                generate(manifest, out, public)
            self.assertEqual(out.read_bytes(), first)


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
            # Identical source VS reused by another effect must not force a
            # second compiled Vulkan module; the ordinal pass still exists.
            shared=[dict(row) for row in entries]
            shared[4]['sha256']=entries[0]['sha256']
            save(shared)
            result=generate(manifest,out,src)
            self.assertEqual(result['ordinal_pair_candidates'],3)
            shared_output=out.read_bytes()
            self.assertEqual(shared_output[16+2*72+8:16+2*72+40],
                             bytes.fromhex(entries[0]['sha256']))
            # A single original container SHA cannot change from VS to PS.
            conflicting=[dict(row) for row in entries]
            conflicting[5]['sha256']=entries[0]['sha256']
            save(conflicting)
            with self.assertRaises(ValueError):generate(manifest,out,src)
            self.assertEqual(out.read_bytes(),shared_output)
            save(entries)
            result=generate(manifest,out,src)
            self.assertEqual(out.read_bytes(),original)
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
