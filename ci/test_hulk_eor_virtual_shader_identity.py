#!/usr/bin/env python3
"""Original Xenos virtual-header identity; synthetic owner manifest regression.

All shader records here are freshly synthesized 40-byte mock headers plus
non-executable 4-byte physical payloads. No game XEX or proprietary shader.
"""
from pathlib import Path
import hashlib
import json
import struct
from tempfile import TemporaryDirectory
import unittest

from hulk_eor_virtual_shader_identity import extract, MAGIC, ENTRY

def shader(stage: str, code: bytes=b"DATA") -> bytes:
    if stage not in ("VS","PS") or not len(code):
        raise ValueError("bad synthetic source")
    return struct.pack(">9I",0x102A1101 if stage=="VS" else 0x102A1100,
                       36,len(code),0,0,0,0,0,0)+code


def setup_source(root: Path, *, real_list=True):
    directory=root/"private"/"source"
    (directory/"Xenos").mkdir(parents=True)
    rows=[]
    for stage,material in (("VS",b"TEST"),("PS",b"DATA")):
        body=shader(stage,material)
        digest=hashlib.sha256(body).hexdigest()
        (directory/"Xenos"/(digest+".xenos.bin")).write_bytes(body)
        rows.append({"digest":digest,"sha256":digest,"stage":stage,
                     "len":len(body),"bytes":len(body)})
    if real_list:
        obj=[{"key":123,"name":"Synthetic","effect_sha256":"f"*64,
              "size":80,"shaders":rows}]
    else:
        obj={"bank_version":1,"platform":"Xbox360",
             "shader_containers":rows}
    path=directory/"source_manifest_private.json"
    path.write_text(json.dumps(obj),encoding="utf-8")
    return path,rows

class OriginalVirtualShaderCrosswalkTests(unittest.TestCase):
    def test_complete_owner_source_fingerprint_bank_is_source_pinned(self):
        witness=Path(__file__).resolve().parents[1]/(
            "native_port/analysis/"
            "hulk_usa_original_virtual_shader_sha_source_2026-11-21.json")
        source=json.loads(witness.read_text(encoding="utf-8"))
        self.assertEqual(source["private_bank_magic"],MAGIC.decode("ascii"))
        self.assertEqual(source["original_virtual_to_full_source_entries"],5194)
        self.assertEqual(source["original_full_xenos_shader_stage_programs"],5194)
        self.assertEqual(source["original_virtual_header_sha256_collisions"],0)
        self.assertEqual(source["source_verified_private_bank_bytes"],
                         16+5194*ENTRY.size)
        self.assertEqual(source["source_verified_private_bank_sha256"],
                         "82064e54dd6ca98796b08cc92b1f7063321d8e4ca"
                         "4386bfe861e4569d54f22cd")
        self.assertFalse(source[
            "runtime_original_xdk_shader_object_to_virtual_header_pointer_abi_verified"])
        self.assertFalse(source[
            "runtime_source_shader_microcode_digest_at_current_xdk_draw_verified"])
        self.assertFalse(source["native_eor_vulkan_gameplay_frame_verified"])
        self.assertTrue(source[
            "no_original_guest_program_code_or_original_shader_bytes_published"])

    def test_real_private_owner_list_and_legacy_manifest_generate_identical_bank(self):
        with TemporaryDirectory() as temp:
            root=Path(temp)
            source,rows=setup_source(root)
            private=root/"private"/"hulk_shader_virtual_fingerprints.bin"
            public=root/"repo"
            public.mkdir()
            result=extract(source,private,public)
            self.assertEqual(result["format"],"HEORVRT1")
            self.assertEqual(result["source_original_full_shader_count"],2)
            self.assertEqual(result["unique_original_virtual_shader_headers"],2)
            self.assertEqual(result["ambiguous_original_virtual_shader_headers"],0)
            self.assertEqual(result["highest_original_virtual_shader_header_bytes"],36)
            self.assertFalse(result["runtime_bound_xdk_shader_object_header_offset_verified"])
            self.assertFalse(result["runtime_full_material_and_texture_ownership_verified"])
            content=private.read_bytes()
            self.assertEqual(content[:8],MAGIC)
            self.assertEqual(struct.unpack_from("<II",content,8),(2,1))
            self.assertEqual(len(content),16+2*ENTRY.size)
            self.assertEqual(ENTRY.size,72)
            members=[]
            for pos in range(16,len(content),ENTRY.size):
                vhash,original,stage,size=ENTRY.unpack_from(content,pos)
                self.assertEqual(size,36)
                self.assertIn(stage,(1,2))
                members.append((vhash,original,stage,size))
            self.assertEqual(
                {row[1].hex() for row in members},
                {r["digest"] for r in rows})
            for vhash,original,stage,_ in members:
                original_source=(source.parent/"Xenos"/
                                (original.hex()+".xenos.bin")).read_bytes()
                self.assertEqual(hashlib.sha256(original_source[:36]).digest(),vhash)
                self.assertEqual(stage,1 if original_source[3]&1 else 2)
            first_sha=hashlib.sha256(content).hexdigest()
            self.assertEqual(
                result["private_virtual_to_original_sha256_bank_sha256"],
                first_sha)
            legacy_path,_=setup_source(root/"legacy",real_list=False)
            legacy_result=extract(legacy_path,private,public)
            self.assertEqual(private.read_bytes(),content)
            self.assertEqual(legacy_result["format"],"HEORVRT1")

    def test_ambiguous_virtual_metadata_cannot_claim_original_source_identity(self):
        with TemporaryDirectory() as temp:
            root=Path(temp);source,rows=setup_source(root)
            old=source.parent/"Xenos"/(rows[0]["digest"]+".xenos.bin")
            data=old.read_bytes()
            # Same virtual header, different *physical* body -> two valid
            # original full SHA values with one ambiguous virtual identity.
            variant=data[:36]+b"XXXX"
            same_virtual_new_full=hashlib.sha256(variant).hexdigest()
            (source.parent/"Xenos"/
             (same_virtual_new_full+".xenos.bin")).write_bytes(variant)
            manifest=json.loads(source.read_text())
            manifest[0]["shaders"].append({
                "digest":same_virtual_new_full,
                "stage":"VS","len":len(variant)})
            source.write_text(json.dumps(manifest),encoding="utf-8")
            with self.assertRaisesRegex(ValueError,"Ambiguous virtual"):
                extract(source,root/"private"/"out.bin",root/"public")

    def test_changed_source_or_public_destination_is_rejected(self):
        with TemporaryDirectory() as temp:
            root=Path(temp);source,rows=setup_source(root)
            private=root/"private"/"out.bin"
            repo=root/"repo";repo.mkdir()
            original=extract(source,private,repo)
            content=private.read_bytes()
            with self.assertRaisesRegex(ValueError,"must not be published"):
                extract(source,repo/"not-private.bin",repo)
            self.assertEqual(private.read_bytes(),content)
            damaged=source.parent/"Xenos"/(rows[0]["digest"]+".xenos.bin")
            damaged.write_bytes(damaged.read_bytes()[:-1])
            with self.assertRaisesRegex(ValueError,"source identity mismatch"):
                extract(source,private,repo)
            self.assertEqual(private.read_bytes(),content)

if __name__=="__main__":
    unittest.main()
