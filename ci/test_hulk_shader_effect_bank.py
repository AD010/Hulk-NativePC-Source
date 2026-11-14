#!/usr/bin/env python3
"""Source-only synthetic EoR effect bank tests (no original shader bytes)."""
import json
import struct
import tempfile
import unittest
from pathlib import Path
from hulk_shader_effect_bank import make_bank, scan_xenos_containers

def synthetic_index(big_endian=True):
    fmt = '>' if big_endian else '<'
    name = b'ShaderEffects\0'
    p0, p1 = 16, 16 + len(name)
    p2 = p1 + 4 + 2 * 4 + 2 * 12
    return (struct.pack(fmt+'4I', 2, p0, p1, p2) + name +
            struct.pack(fmt+'3I', 2, 1, 2) +
            struct.pack(fmt+'6I', 4, 3, 0, 7, 4, 0) +
            struct.pack(fmt+'I', 0))

class ShaderEffectBankTest(unittest.TestCase):
    def test_private_full_bank_and_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            index = root/'xbox.ind'
            arc = root/'SHADEREF.ARC'
            repo = root/'public'
            repo.mkdir()
            index.write_bytes(synthetic_index())
            arc.write_bytes(b'HEADABCDEFG')
            private = root/'private'
            report = make_bank(index,arc,private,repo_root=repo,extract=True)
            self.assertEqual(report['original_effects'],2)
            self.assertFalse(report['ready_for_xenosrecomp'])
            self.assertEqual((private/'00000001.effect.bin').read_bytes(),b'ABC')
            self.assertEqual((private/'00000002.effect.bin').read_bytes(),b'DEFG')
            self.assertEqual(json.loads((private/'manifest.json').read_text())['records'],
                             report['records'])
            self.assertEqual(make_bank(index,arc,private,repo_root=repo,extract=True), report)
            with self.assertRaises(ValueError):
                make_bank(index,arc,repo/'shaders',repo_root=repo,extract=True)
            (private/'00000001.effect.bin').write_bytes(b'INCORRECT')
            with self.assertRaises(ValueError):
                make_bank(index,arc,private,repo_root=repo,extract=True)
            index.write_bytes(synthetic_index(False))
            with self.assertRaises(ValueError):
                make_bank(index,arc,private,repo_root=repo,extract=False)

    def test_standalone_xenos_containers_for_xenosrecomp(self):
        def container(flag):
            header=struct.pack('>9I',flag,64,12,0,40,0,48,0,0)
            return header+b"\0"*(64-len(header))+b"VALIDCODE123"
        vs=container(0x102A1101)
        ps=container(0x102A1100)
        stream=b"TECHNIQUE\0"+vs+b"MATERIAL\0"+ps
        records=list(scan_xenos_containers(stream))
        self.assertEqual(len(records),2)
        self.assertEqual((records[0][1],records[1][1]),('VS','PS'))
        self.assertEqual(records[0][2],vs)
        self.assertEqual(records[1][2],ps)
        invalid=bytearray(vs)
        struct.pack_into('>I',invalid,4,0xFFFFFFFF)
        with self.assertRaises(ValueError):
            list(scan_xenos_containers(bytes(invalid)))
        invalid=bytearray(vs)
        struct.pack_into('>I',invalid,28,1)
        with self.assertRaises(ValueError):
            list(scan_xenos_containers(bytes(invalid)))
        self.assertEqual(list(scan_xenos_containers(b'NO XENOS SHADER')),[])

    def test_bounds_and_overlap(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            index=root/'xbox.ind'
            arc=root/'SHADEREF.ARC'
            arc.write_bytes(b'HEADABCDEFG')
            clean=synthetic_index()
            index.write_bytes(clean)
            def bad_at(off, number):
                buf=bytearray(clean)
                struct.pack_into('>I',buf,off,number)
                index.write_bytes(buf)
                with self.assertRaises(ValueError):
                    make_bank(index,arc,root/'private',repo_root=root/'public',extract=False)
            # Table layout after 16-byte header + 14-byte category name.
            table=30
            bad_at(table+12,99999)  # record0 offset beyond file
            bad_at(table+16,0)      # record0 must be non-empty
            bad_at(table+24,6)      # record1 starts inside record0 [4,7)
            bad_at(table+20,1)      # reserved bit must be zero

if __name__ == '__main__':
    unittest.main()
