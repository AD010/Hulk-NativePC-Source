#!/usr/bin/env python3
"""Synthetic compiler-protocol tests. Never require original Hulk shader bytes."""
import hashlib
import json
import os
import sys
from pathlib import Path
import struct
import tempfile
import unittest
from hulk_xenos_to_spirv import spirv_info, encode_bank, manifest_entries, compile_private_bank, ADAPTER_MARKER, GRADIENT_ADAPTER_MARKER


def fake_spv(stage):
    words = [0x07230203, 0x00010500, 0, 12, 0,
             0x00020011, 1, 0x0003000e, 0, 1,
             0x0005000f, 0 if stage == 'VS' else 4, 1, 0x6e69616d, 0]
    return struct.pack('<' + 'I' * len(words), *words)


class OfflineShaderTests(unittest.TestCase):
    def test_structural_stage(self):
        self.assertEqual(spirv_info(fake_spv('VS'), 'VS'), 15)
        self.assertEqual(spirv_info(fake_spv('PS'), 'PS'), 15)
        with self.assertRaises(ValueError): spirv_info(fake_spv('VS'), 'PS')
        with self.assertRaises(ValueError): spirv_info(fake_spv('VS')[:-4], 'VS')
        with self.assertRaises(ValueError): spirv_info(b'', 'VS')
        with self.assertRaises(ValueError): spirv_info(fake_spv('VS') + b'\0', 'VS')

    def test_bank_layout_and_reject_duplicate(self):
        vs = ('ab' * 32, 'VS', fake_spv('VS'))
        ps = ('cd' * 32, 'PS', fake_spv('PS'))
        packed = encode_bank([vs, ps])
        self.assertEqual(packed[:8], b'HEORSPV1')
        self.assertEqual(struct.unpack_from('<II', packed, 8), (2, 2))
        self.assertEqual(packed[16:48], bytes.fromhex(vs[0]))
        self.assertEqual(struct.unpack_from('<II', packed, 48), (1, 15))
        with self.assertRaises(ValueError): encode_bank([vs, vs])
        with self.assertRaises(ValueError): encode_bank([('ab' * 32, 'PS', fake_spv('VS'))])

    def test_private_sources_and_atomic_full_coverage(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            root = folder / 'private'
            (root / 'Xenos').mkdir(parents=True)
            entries = []
            for stage, value in [('VS', b'A' * 48), ('PS', b'B' * 48)]:
                digest = hashlib.sha256(value).hexdigest()
                (root / 'Xenos' / (digest + '.xenos.bin')).write_bytes(value)
                entries.append({'sha256': digest, 'stage': stage, 'bytes': 48})
            manifest = {'bank_version': 1, 'platform': 'Xbox360',
                        'shader_containers': entries}
            manifest_path = root / 'manifest.json'
            manifest_path.write_text(json.dumps(manifest))
            script = folder / 'fake_tool.py'
            script.write_text('''#!/usr/bin/env python3
import pathlib,sys,struct
args=sys.argv[1:]
if '-Fo' in args:
    pathlib.Path(__file__).with_suffix('.dxc_args').write_text(' '.join(args))
    stage='VS' if 'vs_6_0' in args else 'PS'
    words=[0x07230203,0x10500,0,12,0,0x20011,1,0x3000e,0,1,0x5000f,
           0 if stage=='VS' else 4,1,0x6e69616d,0]
    pathlib.Path(args[args.index('-Fo')+1]).write_bytes(struct.pack('<15I',*words))
elif '--target-env' not in args:
    pathlib.Path(args[1]).write_text('// HULK_EOR_XENOSRECOMP_STRICT_LOD2D_V1\\n// HULK_EOR_XENOSRECOMP_GRAD2D_V1\\nvoid main() {}')
''')
            script.chmod(0o755)
            if os.name == 'nt':
                # Windows CreateProcess cannot execute a Python source file as
                # a native executable. Exercise the same subprocess protocol.
                wrapper = folder / 'fake_tool.cmd'
                wrapper.write_text('@echo off\r\n"' + sys.executable +
                                   '" "' + str(script) + '" %*\r\n')
                script = wrapper
            header = folder / 'shader_common.h'
            header.write_text('// synthetic')
            output = root / 'native.spvbank'
            result = compile_private_bank(manifest_path, output,
                                          xenosrecomp=script, dxc=script,
                                          spirv_val=script, shader_common=header,
                                          repo_root=folder / 'public')
            self.assertEqual(result['source_shaders'], 2)
            self.assertEqual(result['compiled'], 2)
            flags = script.with_suffix('.dxc_args').read_text()
            self.assertIn('-fspv-reflect', flags)
            self.assertNotIn('-Qstrip_debug', flags)
            self.assertFalse(result['runtime_material_binding_verified'])
            self.assertEqual(ADAPTER_MARKER, 'HULK_EOR_XENOSRECOMP_STRICT_LOD2D_V1')
            self.assertEqual(GRADIENT_ADAPTER_MARKER, 'HULK_EOR_XENOSRECOMP_GRAD2D_V1')
            saved = output.read_bytes()
            self.assertEqual(saved[:8], b'HEORSPV1')
            (root / 'Xenos' / (entries[1]['sha256'] + '.xenos.bin')).write_bytes(b'X' * 48)
            with self.assertRaises(ValueError):
                compile_private_bank(manifest_path, output,
                                     xenosrecomp=script, dxc=script, spirv_val=script,
                                     shader_common=header, repo_root=folder / 'public')
            self.assertEqual(output.read_bytes(), saved)
            with self.assertRaises(ValueError): manifest_entries({}, root)
            with self.assertRaises(ValueError):
                compile_private_bank(manifest_path, folder / 'public' / 'bank',
                                     xenosrecomp=script, dxc=script, spirv_val=script,
                                     shader_common=header, repo_root=folder / 'public')


if __name__ == '__main__':
    unittest.main()
