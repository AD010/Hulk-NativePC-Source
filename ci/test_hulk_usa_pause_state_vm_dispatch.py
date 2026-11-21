#!/usr/bin/env python3
"""Synthetic verification of EoR original Pause VM -> global PPC callback.

No proprietary script opcode buffers or USA guest instructions in fixtures.
"""
import json
from pathlib import Path
import unittest
import zlib

from hulk_usa_pause_state_vm_dispatch import (
    STATE_GETTER_HASH, STATE_NATIVE_NAME_GUEST,
    STATE_NATIVE_HANDLER_GUEST, STATE_NATIVE_REGISTRATION_GUEST,
    GUEST_BASE, original_pause_state, registered_native_state, inspect,
)

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/"native_port/analysis/hulk_usa_pause_state_r_vm_witness_2026-11-21.json"


def vm_script(endian):
    # Synthetic 13-byte instruction with real global state_r HASH, but
    # no original copyrighted VM payload: the test exercises the
    # source method framing, endianness and CRC32 identifier independently.
    body=bytes([0x10,0x11,0x12,0x13,0x28])+STATE_GETTER_HASH.to_bytes(
        4,endian)+bytes([0x14,0x15,0x16,0x17])
    return (bytes([0])*128+b"UIPage_Pause_Settings\x00"+bytes([0])*24+
            b"ChangeState_r\x00"+len(body).to_bytes(4,endian)+body+
            (zlib.crc32(b"CHANGESTATE_R")&0xffffffff).to_bytes(4,endian))


def synthetic_mapped_image():
    data=bytearray(0x1400000)
    data[:2]=b"MZ"
    data[STATE_NATIVE_NAME_GUEST-GUEST_BASE:
         STATE_NATIVE_NAME_GUEST-GUEST_BASE+8]=b"state_r\x00"
    for addr,word in (
        (STATE_NATIVE_REGISTRATION_GUEST,STATE_NATIVE_NAME_GUEST),
        (STATE_NATIVE_REGISTRATION_GUEST+4,STATE_NATIVE_HANDLER_GUEST),
        (STATE_NATIVE_REGISTRATION_GUEST+8,0x82002E4C),
        (STATE_NATIVE_REGISTRATION_GUEST+12,0x82002E4C),
        # Standard PPC mr r31,r3 (a synthetic register test instruction).
        (STATE_NATIVE_HANDLER_GUEST+12,0x7C7F1B78),
    ):
        data[addr-GUEST_BASE:addr-GUEST_BASE+4]=word.to_bytes(4,"big")
    return bytes(data)


class OriginalPauseVmTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = json.loads(SOURCE.read_text(encoding="utf-8"))
        cls.image = synthetic_mapped_image()

    def test_original_pause_method_calls_name_hash_not_guest_pointer(self):
        xbox=original_pause_state(vm_script("big"),"big")
        pc=original_pause_state(vm_script("little"),"little")
        self.assertEqual(xbox,pc)
        self.assertEqual(xbox["invoked_original_global_name"],"state_r")
        self.assertEqual(xbox["invoked_global_crc32_upper"],"F7A5B1D1")
        self.assertFalse(xbox["state_r_is_unique_pause_callback"])
        self.assertEqual(zlib.crc32(b"STATE_R")&0xffffffff,STATE_GETTER_HASH)

    def test_original_registered_ppc_global_vm_context_arg(self):
        result=registered_native_state(self.image,strict_source=False)
        self.assertEqual(result["registered_original_native_ppc_handler"],
                         "0x8264C798")
        self.assertEqual(result["first_native_ppc_context_arg"],"r3")
        self.assertFalse(result["actual_pause_page_event_dispatcher_abi_verified"])
        self.assertFalse(result["vm_native_parameter_and_return_types_fully_verified"])

    def test_public_source_witness_matches_reproduced_synthetic_abi(self):
        result=inspect(vm_script("big"),vm_script("little"),self.image,
                       strict_source=False)
        self.assertEqual(result,self.original)
        self.assertTrue(result["global_vm_getter_is_not_unique_pause_ui_dispatcher"])
        self.assertFalse(result[
            "original_ui_page_pause_settings_native_windows_binding_installed"])

    def test_wrong_guest_method_registration_refuses_to_prove_ppc_abi(self):
        for address in (STATE_NATIVE_REGISTRATION_GUEST,
                        STATE_NATIVE_REGISTRATION_GUEST+4,
                        STATE_NATIVE_REGISTRATION_GUEST+8,
                        STATE_NATIVE_REGISTRATION_GUEST+12,
                        STATE_NATIVE_HANDLER_GUEST+12):
            with self.subTest(address=hex(address)):
                b=bytearray(self.image)
                b[address-GUEST_BASE:address-GUEST_BASE+4]=bytes([0])*4
                with self.assertRaises(ValueError):
                    registered_native_state(bytes(b),strict_source=False)

    def test_real_original_vm_native_callback_is_separate_from_xdk_and_ui(self):
        root=ROOT/"native_port"
        rows=(root/"analysis/guest_host_mapping.csv").read_text(
            encoding="utf-8-sig")
        self.assertIn("0x8264C798,0x001427F9EA0",rows)
        hooks=(root/"native_hook_addresses.txt").read_text(encoding="utf-8")
        self.assertIn("8264C798",hooks)
        generated=(root/"tools/gen_native_x_hooks.py").read_text(
            encoding="utf-8")
        self.assertIn('SOURCE_EOR_VM_METHODS = {"8264C798"}',generated)
        self.assertIn("HULK_EOR_VM_STATE_TRACE_HOOK",generated)
        self.assertIn("ObserveOriginalEorVmState_r(0x##addr,ctx,base)",generated)
        self.assertIn("__imp__sub_##addr(ctx,base);",generated)
        bridge=(root/"src/eor_xdk_hook_bridge.cpp").read_text(encoding="utf-8")
        self.assertIn("void ObserveOriginalEorVmState_r(",bridge)
        self.assertIn("original_method_guest!=0x8264C798u",bridge)
        self.assertIn("const auto vm_frame=read32(original_ctx.r3.u32)",bridge)
        self.assertIn("PAGE-OWNER UNKNOWN",bridge)
        self.assertNotIn("UIPage_Pause_Settings event dispatched",bridge)
        # The original global method is NOT a render device operation.
        import configparser
        cfg=configparser.ConfigParser(interpolation=None)
        self.assertTrue(cfg.read(root/"hulk_native_x_profile.ini"))
        self.assertNotIn("0x8264C798",cfg["hooks"])

    def test_corrupted_original_pause_opcode_name_hash_and_platform_rejected(self):
        for endian in ("big","little"):
            script=vm_script(endian)
            key=bytes([0x28])+STATE_GETTER_HASH.to_bytes(4,endian)
            self.assertEqual(script.count(key),1)
            for broken in (
                script.replace(key,key.replace(bytes([0x28]),bytes([0x29]),1)),
                script.replace(key,key[:1]+bytes([0])*4),
                script.replace(b"UIPage_Pause_Settings\x00",
                               b"UIPage_Pause_Xettings\x00"),
                script+ b"UIPage_Pause_Settings\x00",
                script.replace(b"ChangeState_r\x00",b"ChangeState_X\x00"),
            ):
                with self.subTest(endian=endian,case=broken[-20:]):
                    with self.assertRaises(ValueError):
                        original_pause_state(broken,endian)
        with self.assertRaises(ValueError):
            original_pause_state(vm_script("big"),"arbitrary")


if __name__=="__main__":
    unittest.main()
