#!/usr/bin/env python3
"""Deterministic synthetic test for executable XenosRecomp->DXC AOT pipeline."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
from hulk_xenos_aot import compile_bank, _check_spirv, _entries

def bank(root):
    src = root / "bank" / "Xenos"
    src.mkdir(parents=True)
    code = struct.pack(">9I",0x102A1101,64,12,0,40,0,48,0,0) + b"\0"*40
    sha = hashlib.sha256(code).hexdigest()
    (src / (sha+".xenos.bin")).write_bytes(code)
    row = {"sha256":sha,"stage":"VS","bytes":len(code),
           "private_file":f"Xenos/{sha}.xenos.bin","effect":"AABBCCDD"}
    (src.parent / "manifest.json").write_text(json.dumps({
        "platform":"Xbox360","bank_version":1,
        "archive_sha256":"f"*64,"shader_containers":[row]}))
    return src.parent, sha

class AotTests(unittest.TestCase):
    def test_compile_and_integrity(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            b, sha = bank(root)
            repo = root / "public"
            repo.mkdir()
            out = root / "private-output"
            xenos = root / "XenosRecomp.exe"
            dxc = root / "dxc.exe"
            common = root / "shader_common.h"
            for f in (xenos,dxc,common):
                f.write_bytes(b"synthetic")
            calls = []
            def run(args, **kwargs):
                calls.append(args)
                if len(calls) % 2:
                    Path(args[2]).write_text("void main() {}\n")
                else:
                    Path(args[args.index("-Fo")+1]).write_bytes(
                        struct.pack("<5I",0x07230203,0x10500,1,42,0)+b"\0"*4)
                return type("Result",(),{"returncode":0})()
            with patch("hulk_xenos_aot.subprocess.run",side_effect=run):
                report=compile_bank(b,out,xenos,dxc,common,repo)
            self.assertEqual(report["compiled_count"],1)
            self.assertEqual(report["failed_count"],0)
            self.assertFalse(report["native_renderer_ready"])
            self.assertIn("-fvk-invert-y",calls[1])
            self.assertEqual(json.loads((out/"aot_manifest.json").read_text()),report)
            self.assertTrue((out / (sha+".vs.spv")).is_file())
            raw = (out / "hulk_shader_bank.bin").read_bytes()
            self.assertEqual(raw[:8], b"HEORSPV1")
            self.assertEqual(struct.unpack_from("<II", raw, 8), (1, 1))
            self.assertEqual(raw[16:48], bytes.fromhex(sha))
            with self.assertRaises(ValueError):
                compile_bank(b,repo/"cache",xenos,dxc,common,repo)
            (b / "Xenos" / (sha+".xenos.bin")).write_bytes(b"corrupt")
            with self.assertRaises(ValueError):
                _entries(b)
    def test_fail_closed_on_compiler_and_spirv(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            b,_=bank(root)
            repo=root/"public"
            repo.mkdir()
            for name in ("x.exe","d.exe","common.h"):
                (root/name).write_text("stub")
            with patch("hulk_xenos_aot.subprocess.run",return_value=type("Result",(),{"returncode":1})()):
                result=compile_bank(b,root/"out",root/"x.exe",root/"d.exe",root/"common.h",repo)
            self.assertEqual(result["failed_count"],1)
            for data in (b"",b"SPIR-V",struct.pack("<5I",0x07230203,0x10500,0,0,0)):
                with self.assertRaises(ValueError):
                    _check_spirv(data)

if __name__=="__main__":
    unittest.main()
