#!/usr/bin/env python3
"""Synthetic PPC ABI proof: 52 original EoR shader class item renderers.

No original code, XEX, shader/texture bytes or game models in fixtures.
Source 52-class entry witness must match original virtual +44 and the
one true original bctrl caller; unrelated PCs/guest methods are refused.
"""
import json
from pathlib import Path
import re
import unittest

from hulk_usa_shader_render_item_dispatch_abi import inspect,BASE

ROOT=Path(__file__).resolve().parents[1]
DIR=ROOT/"native_port/analysis"
WITNESS=DIR/"hulk_usa_eor_render_item_dispatch_2026-11-21.json"
VTABLES=DIR/"hulk_usa_eor_shader_vtables_2026-11-21.json"

def d(op,dst,base,offset):
    return (op<<26)|(dst<<21)|(base<<16)|(offset&0xffff)

def mr(src,dst):
    return (31<<26)|(src<<21)|(dst<<16)|(src<<11)|0x378

def make_synthetic_shader_item_abi(witness):
    image=bytearray(0x1400000)
    image[:2]=b"MZ"
    opcodes={
        0x82642A98:mr(3,31),
        0x82642AA0:mr(5,23),
        0x82642AF0:d(32,11,30,0),
        0x82642AF4:d(32,28,11,12),
        0x82642BC0:mr(23,5),
        0x82642BC4:mr(28,4),
        0x82642BC8:mr(31,3),
        0x82642BCC:d(32,11,11,44),
        0x82642BD0:0x7D6903A6,
        0x82642BD4:0x4E800421,
    }
    for row in witness["source_verified_class_render_item_methods"]:
        guest=int(row["original_source_guest_vtable"],16)+44
        opcodes[guest]=int(row["original_render_item_method_guest"],16)
    for addr,word in opcodes.items():
        image[addr-BASE:addr-BASE+4]=word.to_bytes(4,"big")
    return image,opcodes


class OriginalEorShaderItemDispatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.witness=json.loads(WITNESS.read_text(encoding="utf-8"))
        cls.vtables=json.loads(VTABLES.read_text(encoding="utf-8"))
        cls.image,cls.instructions=make_synthetic_shader_item_abi(
            cls.witness)

    def test_all_original_52_shader_classes_have_real_distinct_ppc_item_owners(self):
        result=inspect(self.image,self.vtables,strict_source=False)
        self.assertEqual(result,self.witness)
        self.assertEqual(result["original_source_shader_class_count"],52)
        self.assertEqual(result["original_unique_render_item_methods"],22)
        self.assertEqual(result["classes_sharing_original_render_item_method"],27)
        self.assertEqual(result["verified_original_ppc_instruction_count"],10)
        self.assertEqual(result["original_shader_virtual_table_slot_bytes"],44)
        self.assertEqual(result["original_indirect_shader_virtual_call_site"],
                         "0x82642BD4")
        self.assertEqual(result["original_indirect_shader_virtual_call_return_lr"],
                         "0x82642BD8")
        self.assertFalse(result["model_archive_crc32_and_surface_ordinal_verified"])
        self.assertFalse(result["original_material_shader_source_sha256_verified"])

    def test_every_source_ppc_instruction_and_original_class_method_is_required(self):
        for address in self.instructions:
            with self.subTest(original_guest=hex(address)):
                at=address-BASE
                original=self.image[at:at+4]
                self.image[at:at+4]=b"\0"*4
                try:
                    with self.assertRaisesRegex(ValueError,
                                                "Original USA|render item"):
                        inspect(self.image,self.vtables,strict_source=False)
                finally:
                    self.image[at:at+4]=original

    def test_22_real_ppc_entries_are_registered_for_only_source_item_hooks(self):
        root=ROOT/"native_port"
        generator=(root/"tools/gen_native_x_hooks.py").read_text(
            encoding="utf-8")
        mapping=(root/"analysis/guest_host_mapping.csv").read_text(
            encoding="utf-8-sig")
        hooks=(root/"native_hook_addresses.txt").read_text(encoding="utf-8")
        cpp=(root/"src/eor_xdk_hook_bridge.cpp").read_text(encoding="utf-8")
        header=(root/"src/eor_xdk_draw_capture.h").read_text(
            encoding="utf-8")
        witness=self.witness
        methods={x["original_render_item_method_guest"][2:]
                 for x in witness["source_verified_class_render_item_methods"]}
        self.assertEqual(len(methods),22)
        declared=generator.split(
            "SOURCE_EOR_RENDER_ITEM_METHODS = {",1)[1].split("}",1)[0]
        declared_set=set(re.findall(r'"([0-9A-F]{8})"',declared))
        self.assertEqual(declared_set,methods)
        for guest in methods:
            self.assertIn(guest,hooks)
            self.assertIn("0x"+guest+",0x",mapping)
            self.assertIn("case 0x"+guest.lower()+"u:",header)
        self.assertIn("HULK_EOR_RENDER_ITEM_TRACE_HOOK",generator)
        self.assertIn("ScopedOriginalEorRenderItem source_render_item",generator)
        self.assertIn("source_render_item(0x##addr,ctx,base)",generator)
        self.assertIn("__imp__sub_##addr(ctx,base);",generator)
        self.assertIn("static_cast<uint32_t>(original_ctx.lr)!=0x82642BD8u",cpp)
        self.assertIn("original_ctx.r4.u32",cpp)
        self.assertIn("ProvenUsaEorRenderItemMethodVtablePair(",cpp)
        self.assertIn("ValidOriginalEorSourceRenderItem",header)
        self.assertNotIn("original_model_hash=original_ctx.r4.u32",cpp)
        # Runtime source item pointer alone does not establish an ARC
        # resource hash or a runnable native Vulkan scene.
        self.assertTrue(witness["current_render_item_guest_pointer_attestation_available"])
        self.assertFalse(witness["model_archive_crc32_and_surface_ordinal_verified"])

if __name__=="__main__":
    unittest.main()
