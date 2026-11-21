#!/usr/bin/env python3
"""Synthetic original USA shader Decal PPC ABI + native source-hook tests.

No retail PPC instructions, game code or original resource bytes are included.
Instruction fields are freshly assembled from generic ISA encoding rules.
"""
import json
from pathlib import Path
import unittest
from hulk_usa_decal_guest_geometry_abi import audit,GUEST_BASE

ROOT=Path(__file__).resolve().parents[1]
WITNESS=(ROOT/"native_port/analysis/"
         "hulk_usa_decal_geometry_abi_2026-11-21.json")

def ppc_dform(op,dst,base,imm):
    return (op<<26)|(dst<<21)|(base<<16)|(imm&0xffff)

def ppc_mr(src,dst):
    return (31<<26)|(src<<21)|(dst<<16)|(src<<11)|0x378

def ppc_bl(site,dest):
    return (18<<26)|((dest-site)&0x03fffffc)|1

def synthetic_usa_decal_image():
    image=bytearray(0x1400000)
    image[:2]=b"MZ"
    instructions={
        0x8265B42C:ppc_mr(3,31),
        0x8265B448:ppc_mr(7,27),
        0x8265B4CC:ppc_dform(32,11,30,0),
        0x8265B4D8:ppc_dform(32,4,11,12),
        0x8265B4DC:ppc_mr(4,28),
        0x8265B4E0:ppc_bl(0x8265B4E0,0x82642C68),
        0x8265B4E4:ppc_dform(32,11,28,60),
        0x8265B4F8:ppc_dform(14,5,11,24),
        0x8265B500:ppc_bl(0x8265B500,0x8287E650),
        0x8265B50C:ppc_dform(14,4,11,56),
        0x8265B510:ppc_bl(0x8265B510,0x8287E7F8),
        0x8265B520:ppc_dform(14,6,0,0),
        0x8265B524:ppc_dform(14,5,0,0),
        0x8265B528:ppc_dform(14,4,0,4),
        0x8265B538:ppc_bl(0x8265B538,0x8288E318),
    }
    for addr,word in instructions.items():
        off=addr-GUEST_BASE
        image[off:off+4]=word.to_bytes(4,"big")
    return bytes(image),instructions


class OriginalDecalGeometryPpcTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original=json.loads(WITNESS.read_text(encoding="utf-8"))
        cls.image,cls.instructions=synthetic_usa_decal_image()

    def test_source_proven_original_eor_gpu_chain_identified(self):
        proof=audit(self.image,strict_source=False)
        self.assertEqual(proof,self.original)
        self.assertEqual(proof["source_ppc_register_and_branch_checks_passed"],15)
        self.assertTrue(proof["engine_decal_to_guest_vb_ib_abi_verified"])
        self.assertEqual(proof["original_geometry_guest_ptr_be32_offset_from_render_item"],60)
        self.assertEqual(proof["original_geometry_vb_descriptor_offset"],24)
        self.assertEqual(proof["original_geometry_ib_descriptor_offset"],56)
        self.assertFalse(proof["engine_material_effect_pass_shader_pair_verified"])
        self.assertTrue(proof["all_game_code_and_source_assets_remain_private"])

    def test_all_fifteen_real_guest_instruction_witnesses_are_mandatory(self):
        for guest in self.instructions:
            with self.subTest(address=hex(guest)):
                corrupted=bytearray(self.image)
                off=guest-GUEST_BASE
                corrupted[off:off+4]=bytes([0])*4
                with self.assertRaisesRegex(ValueError,"Original USA Decal PPC ABI"):
                    audit(bytes(corrupted),strict_source=False)

    def test_no_implied_material_pass_or_guessed_shader_mapping(self):
        port=ROOT/"native_port"
        header=(port/"src/eor_xdk_draw_capture.h").read_text(
            encoding="utf-8")
        bridge=(port/"src/eor_xdk_hook_bridge.cpp").read_text(
            encoding="utf-8")
        resolver=(port/"src/eor_native_draw_geometry.cpp").read_text(
            encoding="utf-8")
        self.assertIn("ValidOriginalDecalGeometryBinding",header)
        self.assertIn("ctx.r28.u32+0x3Cu",bridge)
        self.assertIn("ctx.r31.u32",bridge)
        self.assertIn("ctx.r27.u32",bridge)
        self.assertIn("geometry+0x18u",bridge)
        self.assertIn("geometry+0x38u",bridge)
        self.assertIn("ValidOriginalDecalGeometryBinding(call)",resolver)
        self.assertIn("SameOriginalEorShaderMethod",resolver)
        self.assertIn("original_decal_xdk_vb_ib_binding_verified",resolver)
        self.assertNotIn("engine_draw_verified=true",bridge[
            bridge.index("// ORIGINAL USA Decal engine PPC"):
            bridge.index("// Keep buffer revisions and their Draw")
        ])


if __name__=="__main__":
    unittest.main()
