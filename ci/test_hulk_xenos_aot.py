#!/usr/bin/env python3
"""Deterministic synthetic test for executable XenosRecomp->DXC AOT pipeline."""
import hashlib
import threading
import time
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
from hulk_xenos_aot import compile_bank, _check_spirv, _entries
from hulk_xenos_to_spirv import ADAPTER_MARKER, GRADIENT_ADAPTER_MARKER

HLSL_FIXTURE = (
    f"// {ADAPTER_MARKER}\n"
    f"// {GRADIENT_ADAPTER_MARKER}\n"
    "void main() {}\n"
)

def fake_legal_spirv(stage: str) -> bytes:
    # Must be a real instruction-framed module: the new native resource
    # preflight correctly rejects the old invalid 6-word dummy payload.
    words = [0x07230203, 0x00010500, 0, 16, 0,
             0x00020011, 1, 0x0003000e, 0, 1,
             0x0005000f, 4 if stage == "PS" else 0,
             1, 0x6e69616d, 0]
    return struct.pack("<" + "I" * len(words), *words)

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
                    Path(args[2]).write_text(HLSL_FIXTURE)
                else:
                    Path(args[args.index("-Fo")+1]).write_bytes(
                        fake_legal_spirv("PS" if "ps_6_0" in args else "VS"))
                return type("Result",(),{"returncode":0})()
            with patch("hulk_xenos_aot.subprocess.run",side_effect=run):
                report=compile_bank(b,out,xenos,dxc,common,repo)
            self.assertEqual(report["compiled_count"],1)
            self.assertEqual(report["failed_count"],0)
            self.assertFalse(report["native_renderer_ready"])
            self.assertIn("-fvk-invert-y",calls[1])
            self.assertEqual(calls[1][calls[1].index("-E")+1],"main")
            self.assertIn("-fspv-reflect",calls[1])
            self.assertNotIn("-Qstrip_debug",calls[1])
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
    def test_parallel_cpu_processes_still_emit_deterministic_bank(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            b, first_sha = bank(root)
            payload = bytes([0x11]) * 80
            second_sha = hashlib.sha256(payload).hexdigest()
            (b / "Xenos" / (second_sha + ".xenos.bin")).write_bytes(payload)
            manifest_path = b / "manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["shader_containers"].append({
                "sha256": second_sha, "stage": "PS",
                "bytes": len(payload), "private_file": f"Xenos/{second_sha}.xenos.bin",
                "effect": "BB112233"})
            manifest_path.write_text(json.dumps(manifest))
            repo = root / "public"
            repo.mkdir()
            for item in ("x.exe", "d.exe", "common.h"):
                (root / item).write_bytes(b"fixture")
            lock = threading.Lock()
            state = {"live": 0, "peak": 0}
            def worker(args, **kwargs):
                with lock:
                    state["live"] += 1
                    state["peak"] = max(state["peak"], state["live"])
                try:
                    time.sleep(0.03)
                    if "-Fo" in args:
                        Path(args[args.index("-Fo")+1]).write_bytes(
                            fake_legal_spirv("PS" if "ps_6_0" in args else "VS"))
                    else:
                        Path(args[2]).write_text(HLSL_FIXTURE)
                    return type("Result",(),{"returncode":0})()
                finally:
                    with lock:
                        state["live"] -= 1
            with patch("hulk_xenos_aot.subprocess.run", side_effect=worker):
                first = compile_bank(b, root/"out", root/"x.exe", root/"d.exe",
                                     root/"common.h",repo,workers=2)
                bank_first = (root/"out"/"hulk_shader_bank.bin").read_bytes()
                second = compile_bank(b, root/"out", root/"x.exe",root/"d.exe",
                                      root/"common.h",repo,workers=2)
                bank_second = (root/"out"/"hulk_shader_bank.bin").read_bytes()
            self.assertEqual(state["peak"],2)
            self.assertEqual(first["workers_used"],2)
            self.assertEqual(first["compiled_count"],2)
            self.assertEqual(first["failed_count"],0)
            self.assertEqual(first["compiled"],second["compiled"])
            self.assertEqual(bank_first,bank_second)
            # A resumable AOT pass must never spawn an external compiler if
            # each previously authenticated shader/host-tool SHA still agrees.
            with patch("hulk_xenos_aot.subprocess.run",
                       side_effect=AssertionError("redundant shader compile")):
                resumed = compile_bank(b, root/"out",root/"x.exe",root/"d.exe",
                                       root/"common.h",repo,workers=2,
                                       resume=True)
            self.assertEqual(resumed["reused_verified_count"],2)
            self.assertEqual(resumed["failed_count"],0)
            self.assertEqual((root/"out"/"hulk_shader_bank.bin").read_bytes(),
                             bank_first)
            self.assertEqual(struct.unpack_from("<II",bank_first,8),(2,2))
            self.assertEqual(set(e["source_sha256"] for e in first["compiled"]),
                             {first_sha,second_sha})
            with self.assertRaises(ValueError):
                compile_bank(b,root/"out",root/"x.exe",root/"d.exe",
                             root/"common.h",repo,workers=17)

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
            # An old XenosRecomp may produce valid-looking HLSL while
            # silently discarding original getCompTexLOD / grad2D semantics.
            # The incremental AOT bank must refuse that output BEFORE DXC.
            def wrong_source(args,**kwargs):
                Path(args[2]).write_text("void main() {}\n")
                return type("Result",(),{"returncode":0})()
            with patch("hulk_xenos_aot.subprocess.run",side_effect=wrong_source):
                stale=compile_bank(
                    b,root/"old-adapter-out",root/"x.exe",root/"d.exe",
                    root/"common.h",repo)
            self.assertEqual(stale["failed_count"],1)
            self.assertEqual(stale["compiled_count"],0)
            for data in (b"",b"SPIR-V",struct.pack("<5I",0x07230203,0x10500,0,0,0)):
                with self.assertRaises(ValueError):
                    _check_spirv(data)

if __name__=="__main__":
    unittest.main()
