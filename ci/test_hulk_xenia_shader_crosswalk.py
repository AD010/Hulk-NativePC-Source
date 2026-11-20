#!/usr/bin/env python3
"""Synthetic Xenos shader crosswalk regression: no private game binary needed."""
from __future__ import annotations
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile
from hulk_xenia_shader_crosswalk import (original_shaders,load_pairs,
                                         inspect_dump_dir,generate)


def container(stage: str, label: bytes, code: bytes) -> bytes:
    assert len(code)==16 and len(label)<28
    flags=0x102A1101 if stage=='VS' else 0x102A1100
    header=struct.pack('>9I',flags,64,16,0,40,0,48,0,0)
    return header+label.ljust(28,b'\0')+code


def fixtures():
    v_code=b'VS-CODE-A-000001'
    p_code=b'PS-CODE-A-000001'
    third_vs=b'VS-CODE-B-000001'
    third_ps=b'PS-CODE-B-000001'
    original=[container('VS',b'v1',v_code),container('PS',b'p1',p_code),
              container('VS',b'v2',v_code),container('PS',b'p2',p_code),
              container('VS',b'v3',third_vs),container('PS',b'p3',third_ps)]
    archive=b''.join(original)
    pairs=[(0x111,0,original[0],original[1]),(0x222,0,original[2],original[3]),
           (0x333,0,original[4],original[5])]
    ledger=bytearray(b'HEORMAT2'+struct.pack('<II',len(pairs),2))
    for h,slot,v,p in pairs:
        ledger.extend(struct.pack('<II',h,slot))
        ledger.extend(hashlib.sha256(v).digest())
        ledger.extend(hashlib.sha256(p).digest())
    return archive,bytes(ledger),v_code,p_code,third_vs


class XeniaShaderCrosswalkTests(unittest.TestCase):
    def test_original_unique_and_ambiguous_passes(self):
        archive,ledger,vs,ps,_=fixtures()
        shaders=original_shaders(archive)
        pairs=load_pairs(ledger,shaders)
        self.assertEqual(len(shaders),6)
        self.assertEqual([p['unambiguous_ucode_pair'] for p in pairs],[False,False,True])
        self.assertEqual([p['effect_hash'] for p in pairs],['00000111','00000222','00000333'])
        with self.assertRaisesRegex(ValueError,'duplicated'):
            broken=bytearray(ledger)
            broken[16+72+8:16+72+72]=broken[16+8:16+72]
            load_pairs(bytes(broken),shaders)
        with self.assertRaisesRegex(ValueError,'stage'):
            broken=bytearray(ledger)
            broken[16+8:16+40]=broken[16+40:16+72]
            load_pairs(bytes(broken),shaders)

    def test_xenia_vulkan_d3d12_shaders_exact_and_swapped(self):
        archive,ledger,vs,ps,_=fixtures()
        with tempfile.TemporaryDirectory() as folder:
            base=Path(folder)
            d=base/'dumps'
            d.mkdir()
            (d/'vulkan_0001.bin.vs').write_bytes(vs)
            swapped=b''.join(ps[i:i+4][::-1] for i in range(0,len(ps),4))
            (d/'dx12_0002.bin.fs').write_bytes(swapped)
            (d/'ignored.spv').write_bytes(b'not an original shader')
            found=inspect_dump_dir(d,original_shaders(archive))
            self.assertEqual(len(found),2)
            self.assertEqual([x['status'] for x in found],['matched','matched'])
            self.assertEqual({x['byte_order'] for x in found},{'exact','word_swapped'})
            self.assertTrue(all(not x['unique_source_shader'] for x in found))
            a=base/'original.arc'; a.write_bytes(archive)
            e=base/'heormat2.bin'; e.write_bytes(ledger)
            output=base/'private'/'crosswalk.json'
            report=generate(a,e,output,dump_dir=d,source_root=base/'public')
            self.assertEqual((report['shader_sources'],report['original_effect_passes']), (6,3))
            self.assertEqual(report['dump_coverage']['matched_files'],2)
            self.assertFalse(report['dump_coverage']['candidates_are_live_draw_evidence'])
            self.assertEqual((report['uniquely_matched_ucode_pairs'],report['ambiguous_ucode_pairs']),(1,2))
            self.assertTrue(output.is_file())
            self.assertNotIn(vs,output.read_bytes())
            with self.assertRaisesRegex(ValueError,'public source tree'):
                generate(a,e,base/'public'/'crosswalk.json',source_root=base/'public')

    def test_zipped_vulkan_and_d3d12_dumps_and_pass_coverage(self):
        archive,ledger,vs,ps,_=fixtures()
        with tempfile.TemporaryDirectory() as folder:
            base=Path(folder)
            source=base/'original.arc'
            source.write_bytes(archive)
            catalog=base/'effects.bin'
            catalog.write_bytes(ledger)
            zipped=base/'captured.zip'
            with zipfile.ZipFile(zipped,'w') as output:
                output.writestr('Vulkan/boot.bin.vs',vs)
                output.writestr('D3D12/menu.bin.fs',ps)
                output.writestr('D3D12/generated/boot.dxil',b'not original')
                output.writestr('Vulkan/generated/boot.spv',b'not original')
            result=generate(source,catalog,base/'private'/'report.json',
                            dump_dir=zipped,source_root=base/'public')
            self.assertEqual(result['dump_coverage']['raw_dump_files'],2)
            self.assertEqual(result['dump_coverage']['matched_files'],2)
            self.assertEqual(result['dump_coverage']['unique_matched_shader_sources'],4)
            self.assertEqual(len(result['dump_coverage']['both_stages_seen_pass_candidates']),2)
            self.assertFalse(result['dump_coverage']['candidates_are_live_draw_evidence'])
            self.assertEqual(
                {x['filename'] for x in result['xenia_dump_matches']},
                {'Vulkan/boot.bin.vs','D3D12/menu.bin.fs'})

    def test_zip_path_traversal_and_oversized_shader_rejected(self):
        archive,ledger,vs,ps,_=fixtures()
        with tempfile.TemporaryDirectory() as folder:
            base=Path(folder)
            source=base/'original.arc';source.write_bytes(archive)
            catalog=base/'effects.bin';catalog.write_bytes(ledger)
            zipped=base/'unsafe.zip'
            with zipfile.ZipFile(zipped,'w') as output:
                output.writestr('../bad.bin.vs',vs)
            with self.assertRaisesRegex(ValueError,'Unsafe Xenia shader ZIP path'):
                generate(source,catalog,base/'private'/'report.json',
                         dump_dir=zipped,source_root=base/'public')
            with zipfile.ZipFile(zipped,'w') as output:
                output.writestr('vulkan/huge.bin.vs',b'x'*(1<<20)+b'abcd')
            with self.assertRaisesRegex(ValueError,'dump size'):
                generate(source,catalog,base/'private'/'report.json',
                         dump_dir=zipped,source_root=base/'public')
            with zipfile.ZipFile(zipped,'w') as output:
                output.writestr('dup.bin.vs',vs)
                output.writestr('dup.bin.vs',vs)
            with self.assertRaisesRegex(ValueError,'Duplicate Xenia shader ZIP member'):
                generate(source,catalog,base/'private'/'report.json',
                         dump_dir=zipped,source_root=base/'public')

    def test_rejects_invalid_archive_and_dump(self):
        with self.assertRaises(ValueError):
            original_shaders(b'garbage')
        archive,ledger,vs,ps,_=fixtures()
        with tempfile.TemporaryDirectory() as folder:
            d=Path(folder)
            (d/'bad.bin.vs').write_bytes(b'1')
            with self.assertRaisesRegex(ValueError,'dump size'):
                inspect_dump_dir(d,original_shaders(archive))


if __name__=='__main__':
    unittest.main()
