#!/usr/bin/env python3
"""Synthetic original-format Xenos shader opcode coverage tests."""
import hashlib
import json
import struct
import tempfile
from pathlib import Path
import unittest
from audit_hulk_eor_shader_coverage import inspect_shader, audit


def shader(stage, opcode=17):
    words = [0] * 9
    words[0] = 1 | (2 << 12) | (5 << 16) # 2 direct fetches starting at #1
    words[1], words[2] = 0x00001000, 0xc0000000
    words[3], words[6] = opcode, 1
    header = struct.pack('>9I', 0x102A1101 if stage == 'VS' else 0x102A1100,
                         64, 36, 0, 40, 0, 40, 0, 0)
    return (header + b'\0' * (40-len(header)) +
            struct.pack('>6I', 0, 36, 0, 0, 0, 0) +
            struct.pack('>9I', *words))


class TestOriginalXenosAudit(unittest.TestCase):
    def test_be_instruction_and_stage(self):
        data = shader('PS')
        report = inspect_shader(data, 'PS')
        self.assertEqual(report['direct_fetches'], {17: 1, 1: 1})
        self.assertFalse(report['dynamic_control_flow_present'])
        with self.assertRaises(ValueError):
            inspect_shader(data, 'VS')
        with self.assertRaises(ValueError):
            inspect_shader(data[:-4], 'PS')

    def test_private_manifest_integrity(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root/'Xenos').mkdir()
            original = shader('VS')
            sha = hashlib.sha256(original).hexdigest()
            path = root/'Xenos'/(sha+'.xenos.bin')
            path.write_bytes(original)
            manifest = root/'manifest.json'
            manifest.write_text(json.dumps({'bank_version': 1,
                 'platform': 'Xbox360', 'shader_containers': [
                    {'sha256': sha, 'bytes': len(original), 'effect': 1234, 'stage': 'VS'}]}))
            self.assertEqual(audit(manifest)['direct_exec_fetch_opcode_counts'], {17: 1, 1: 1})
            path.write_bytes(original[:-1])
            with self.assertRaises(ValueError):
                audit(manifest)


if __name__ == '__main__':
    unittest.main()
