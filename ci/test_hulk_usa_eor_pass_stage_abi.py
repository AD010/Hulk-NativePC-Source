#!/usr/bin/env python3
"""Synthetic original EoR 92-byte technique state to real XDK stage ABI.

All 19 PPC instructions are assembled synthetically from ISA fields.
No source XEX, game shader bytes, source archive or guest memory is public.
"""
import json
from pathlib import Path
import unittest
from hulk_usa_eor_pass_stage_abi import inspect,BASE

ROOT=Path(__file__).resolve().parents[1]
WITNESS=ROOT/"native_port/analysis/hulk_usa_eor_pass_stage_abi_2026-11-21.json"


def ppc_d(op,target,base,displacement):
    return (op<<26)|(target<<21)|(base<<16)|(displacement&0xffff)


def ppc_bl(address,target):
    return (18<<26)|((target-address)&0x03fffffc)|1


def ppc_add(dst,a,b):
    return (31<<26)|(dst<<21)|(a<<16)|(b<<11)|(266<<1)


def synthesized_original_material_abi():
    instructions={
        0x82873108:ppc_d(14,3,3,40),
        0x82873120:ppc_d(14,3,3,872),
        0x82876ED4:ppc_d(14,4,31,40),
        0x82876EE8:ppc_d(32,4,31,24),
        0x82876F2C:ppc_d(14,4,31,872),
        0x82876F40:ppc_d(32,4,31,32),
        0x825F87C0:ppc_d(32,11,4,0),
        0x825F87CC:ppc_d(34,10,31,99),
        0x825F87D0:ppc_d(32,9,31,232),
        0x825F87DC:ppc_d(32,9,31,184),
        0x825F87E8:ppc_d(7,11,11,12),
        0x825F8964:ppc_d(32,28,25,0),
        0x825F89D0:ppc_d(34,11,25,8),
        0x825F9D68:ppc_d(34,11,24,9),
        0x825F9D78:ppc_d(32,11,25,160),
        0x825F9D84:ppc_d(40,9,29,2),
        0x825F9D90:ppc_d(40,11,29,0),
        0x825F9DA4:ppc_d(32,10,11,0),
        0x825F9DA8:ppc_d(32,5,11,4),
        0x825F9DC0:ppc_d(32,5,10,20),
        0x82675DD8:ppc_d(32,5,5,24),
        0x825F84D0:ppc_d(14,11,4,-1),
        0x825F84D4:ppc_d(7,9,11,92),
        0x825F84D8:ppc_d(32,8,31,112),
        0x825F84E0:ppc_d(32,10,31,124),
        0x825F84F4:ppc_d(34,11,30,84),
        0x825F8500:ppc_add(4,11,10),
        0x825F8504:ppc_bl(0x825F8504,0x82675C70),
        0x825F8508:ppc_d(34,11,30,85),
        0x825F850C:ppc_d(32,10,31,136),
        0x825F8518:ppc_add(4,11,10),
        0x825F851C:ppc_bl(0x825F851C,0x82675D20),
        0x82675C90:ppc_d(32,4,30,0),
        0x82675C94:ppc_bl(0x82675C94,0x82874390),
        0x82675D40:ppc_d(32,4,30,0),
        0x82675D44:ppc_bl(0x82675D44,0x82874090),
        0x82874430:ppc_d(36,29,30,12696),
        0x82874114:ppc_d(36,29,30,12692),
        0x826432A4:ppc_d(32,3,30,48),
        0x826432B0:ppc_bl(0x826432B0,0x825F84C0),
        0x825F8570:ppc_d(34,27,30,87),
        0x825F8578:ppc_d(40,30,30,80),
        0x825F8590:ppc_d(32,11,31,196),
        0x825F859C:ppc_d(34,11,30,0),
        0x825F85A0:ppc_d(34,5,30,1),
        0x825F85AC:ppc_bl(0x825F85AC,0x82675E00),
        0x825F85B8:ppc_d(34,6,30,3),
        0x825F85C0:ppc_d(34,5,30,2),
        0x825F85C4:ppc_bl(0x825F85C4,0x82675EA0),
        0x825F85D8:ppc_d(14,26,26,4),
    }
    image=bytearray(0x1400000)
    image[:2]=b"MZ"
    for addr,word in instructions.items():
        off=addr-BASE
        image[off:off+4]=word.to_bytes(4,"big")
    return image,instructions


class OriginalDualStagePassTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image,cls.instructions=synthesized_original_material_abi()
        cls.proof=json.loads(WITNESS.read_text(encoding="utf-8"))

    def test_exact_usa_eor_technique_pass_proof_matches_public_abi(self):
        result=inspect(self.image,strict=False)
        self.assertEqual(result,self.proof)
        self.assertEqual(result["original_source_ppc_instruction_checks"],50)
        self.assertEqual(result["original_runtime_pass_record_stride"],92)
        self.assertEqual(result["original_vs_pass_record_index_byte_offset"],84)
        self.assertEqual(result["original_ps_pass_record_index_byte_offset"],85)
        self.assertEqual(result["original_xdk_vs_device_shader_object_offset"],12696)
        self.assertEqual(result["original_xdk_ps_device_shader_object_offset"],12692)
        self.assertTrue(result["engine_pass_vs_ps_xdk_shader_object_identity_verified"])
        self.assertFalse(result["source_xenos_shader_sha256_to_live_xdk_shader_object_verified"])
        self.assertFalse(result["runtime_state_index_to_source_pdb_pass_ordinal_verified"])
        self.assertFalse(result["original_shader_sampler_gpu_texture_binding_verified"])
        self.assertEqual(result["original_sampler_state_record_count_byte_offset"],87)
        self.assertEqual(result["original_sampler_state_first_record_be16_offset"],80)
        self.assertEqual(result["original_sampler_state_table_guest_offset"],196)
        self.assertEqual(result["original_sampler_state_record_stride"],4)
        self.assertTrue(result["engine_pass_sampler_state_register_list_verified"])
        self.assertFalse(result[
            "engine_pass_sampler_state_equals_shader_texture_fetch_mapping"])

    def test_all_original_pass_pointer_chain_instructions_must_match(self):
        for address in self.instructions:
            with self.subTest(guest=hex(address)):
                offset=address-BASE
                previous=self.image[offset:offset+4]
                self.image[offset:offset+4]=b"\0\0\0\0"
                try:
                    with self.assertRaisesRegex(ValueError,"XDK source drift"):
                        inspect(self.image,strict=False)
                finally:
                    self.image[offset:offset+4]=previous

    def test_pass_observer_separate_from_original_xdk_and_no_fake_material(self):
        port=ROOT/"native_port"
        mapping=(port/"analysis/guest_host_mapping.csv").read_text(
            encoding="utf-8-sig")
        self.assertIn("0x825F84C0,0x001419FCDC0",mapping)
        hooks=(port/"native_hook_addresses.txt").read_text(encoding="utf-8")
        self.assertIn("825F84C0",hooks)
        generator=(port/"tools/gen_native_x_hooks.py").read_text(
            encoding="utf-8")
        self.assertIn('SOURCE_EOR_PASS_METHODS = {"825F84C0"}',generator)
        self.assertIn("HULK_EOR_SHADER_PASS_TRACE_HOOK",generator)
        self.assertIn("ObserveOriginalEorShaderPass(0x##addr,ctx,base)",generator)
        self.assertIn("__imp__sub_##addr(ctx,base)",generator)
        bridge=(port/"src/eor_xdk_hook_bridge.cpp").read_text(
            encoding="utf-8")
        capture=(port/"src/eor_xdk_draw_capture.h").read_text(
            encoding="utf-8")
        resolver=(port/"src/eor_native_draw_geometry.cpp").read_text(
            encoding="utf-8")
        self.assertIn("void ObserveOriginalEorShaderPass(",bridge)
        self.assertIn("original_method_guest!=0x825F84C0u",bridge)
        self.assertIn("*92u",bridge)
        observer=bridge.split("void ObserveOriginalEorShaderPass(",1)[1].split("void ObserveOriginalEorMaterialDescriptor(",1)[0]
        self.assertIn("if(start>UINT32_MAX-91u)return;",observer)
        self.assertIn("std::array<std::byte,92> original_state{}",observer)
        self.assertRegex(observer,r"if\(!ReadGuest\(original_guest_base,static_cast<uint32_t>\(start\),\s*original_state.data\(\),original_state.size\(\)\)\)return;")
        self.assertIn("const auto* shader_slots=original_state.data()+80u;",observer)
        self.assertIn("scope.original_pass_state_record=original_state;",observer)
        self.assertIn("scope.original_pass_state_record_captured=true;",observer)
        self.assertLess(observer.index("if(!ReadGuest(original_guest_base,static_cast<uint32_t>(start)"),observer.index("scope.original_pass_state_record=original_state;"))
        self.assertIn("vs_index=std::to_integer<uint8_t>(shader_slots[4])",bridge)
        self.assertIn("ps_index=std::to_integer<uint8_t>(shader_slots[5])",bridge)
        self.assertIn("8u*vs_index",bridge)
        self.assertIn("8u*ps_index",bridge)
        self.assertIn("owner.original_pass_vs_xdk_guest_object",bridge)
        self.assertIn("owner.original_pass_ps_xdk_guest_object",bridge)
        self.assertIn("ValidOriginalShaderPassStageBinding(",capture)
        self.assertIn("ValidOriginalShaderPassSamplerStateAtDraw(",capture)
        self.assertIn("const auto sampler_table=read32(*material_ctx+196u)",bridge)
        self.assertIn("std::to_integer<uint8_t>(shader_slots[7])",bridge)
        self.assertIn("sampler state register writes are source-verified",capture.lower())
        self.assertIn("original_pass_sampler_slots_verified",bridge)
        self.assertIn("original_pass_live_texture_objects_present",bridge)
        self.assertIn("ValidOriginalShaderPassStageBinding(original_call)",resolver)
        self.assertIn("ValidOriginalShaderPassStageBinding(call)",resolver)
        self.assertNotIn("original_guest_resource_to_material_mapping_verified=true",
                         bridge)


if __name__=="__main__":
    unittest.main()
