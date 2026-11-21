#!/usr/bin/env python3
"""Synthetic original EoR UI VM route tests; no proprietary archive in source."""
import unittest
import zlib
import json
from pathlib import Path
from audit_hulk_original_pause_ui_objects import (
    PAUSE_SEQUENCE, inspect_actions, inspect_objects,
    original_pause_ui_field_refs,
)


def object_fixture(endian="big", extra_after_options=0):
    methods = {
        "uiwidget_pausemenu_z", "ChangeState_r", "Init_z",
        "ActivateUpgradeMenu_z", "ActivateCurrentStatusMenu_z",
    }
    parts = []
    for name in PAUSE_SEQUENCE:
        parts.append(name.encode("ascii") + b"\0")
        if name in methods:
            if name == "ChangeState_r":
                # Synthetic opaque VM operand with the original 13-byte shape,
                # never a copied original game opcode program.
                payload = b"\x31\x02\x37\x01\x28" + (0xDEADBEEF).to_bytes(4, endian) + b"\x0e\x02\x35\x00"
            else:
                payload = b"\x31\x02"
            parts.append(len(payload).to_bytes(4, endian))
            parts.append(payload)
            parts.append((zlib.crc32(name.upper().encode()) & 0xffffffff).to_bytes(4, endian))
            parts.append(b"\0" * 7)
        elif name == "OptionsMenuState":
            parts.append(b"\x01" * extra_after_options)
        else:
            parts.append(b"\x01" * 16)
    parts.append(b"\x02" * 32)
    parts.append(b"uipage_pause_gamesettings_z\0")
    parts.append(b"\x03" * 40)
    parts.append(b"Init_z\0")
    return b"".join(parts)


def field_fixture(endian):
    # Synthetic method bodies; only source-name CRCs, never original EoR
    # method bytecode or original game content.
    names = {
        "Init_z": "inputDeviceListener_Pause",
        "StateEnter_z": "CurrentStatusMenu",
        "StateExit_z": "Hidden",
        "ActivateUpgradeMenu_z": "CurrentStatusMenu",
    }
    parts = [b"UIPage_Pause_Settings" + bytes([0])]
    for method, owner in names.items():
        identity = (zlib.crc32(owner.upper().encode("ascii")) &
                    0xffffffff).to_bytes(4, endian)
        payload = b"\x10" + identity + b"\x20"
        parts.extend((method.encode() + b"\0",
                      len(payload).to_bytes(4, endian),
                      payload, b"\0" * 4))
    return b"".join(parts)


class OriginalPauseFieldHashTests(unittest.TestCase):
    def test_authentic_field_hash_references_are_endian_invariant(self):
        xbox = original_pause_ui_field_refs(
            field_fixture("big"), "xbox_360_source")
        pc = original_pause_ui_field_refs(
            field_fixture("little"), "pc_2008_comparator")
        self.assertEqual(xbox, pc)
        self.assertEqual(xbox["Init_z"]["source_field_references"][0],
                         {"vm_byte_offset": 1,
                          "field_name": "inputDeviceListener_Pause",
                          "source_crc32_upper": "{:08X}".format(
                              zlib.crc32(b"INPUTDEVICELISTENER_PAUSE") & 0xffffffff)})
        self.assertEqual(xbox["StateEnter_z"]["source_field_references"][0]
                         ["field_name"], "CurrentStatusMenu")
        self.assertEqual(xbox["StateExit_z"]["source_field_references"][0]
                         ["field_name"], "Hidden")

    def test_original_cross_platform_verified_41_field_references_are_identical(self):
        report = json.loads((Path(__file__).resolve().parents[1] /
            "native_port/analysis/eor_original_pause_ui_source_witness_2026-11-21.json"
            ).read_text(encoding="utf-8"))
        sources = report["sources"]
        xbox = sources["xbox360_object_archive"]["pause_ui_field_hash_references"]
        pc = sources["pc_2008_comparator"]["pause_ui_field_hash_references"]
        self.assertEqual(xbox, pc)
        self.assertEqual({m: len(v["source_field_references"]) for m,v in xbox.items()},
                         {"Init_z": 13, "StateEnter_z": 25,
                          "StateExit_z": 2, "ActivateUpgradeMenu_z": 1})
        self.assertTrue(report["pause_ui_vm_41_field_refs_match_original_xbox_and_pc_endianness"])
        self.assertTrue(report["pause_ui_vm_hash_refs_do_not_prove_ppc_event_dispatch_abi"])
        for method in xbox.values():
            for row in method["source_field_references"]:
                self.assertEqual(row["source_crc32_upper"],
                    "{:08X}".format(zlib.crc32(
                        row["field_name"].upper().encode("ascii")) & 0xffffffff))

    def test_vm_field_hash_reader_rejects_wrong_size_and_platform(self):
        original = field_fixture("big")
        with self.assertRaises(ValueError):
            original_pause_ui_field_refs(original, "arbitrary_guest")
        with self.assertRaises(ValueError):
            original_pause_ui_field_refs(
                original.replace(b"StateEnter_z" + bytes([0]),
                                 b"NotStateEnt_z" + bytes([0])),
                "xbox_360_source")
        with self.assertRaises(ValueError):
            original_pause_ui_field_refs(
                original.replace(b"Init_z" + bytes([0]) + (6).to_bytes(4, "big"),
                                 b"Init_z" + bytes([0]) + (2000).to_bytes(4, "big")),
                "xbox_360_source")

class OriginalPauseScriptTests(unittest.TestCase):
    def test_source_route_and_game_settings_init(self):
        data = object_fixture()
        out = inspect_objects(data, "xbox_360_source")
        self.assertEqual(list(out["pause_route_offsets_from_page"]), list(PAUSE_SEQUENCE))
        self.assertTrue(out["game_settings_script_present"])
        self.assertGreater(out["game_settings_init_relative"], 0)
        self.assertEqual(out["change_state_vm_opaque_0x28_operand"], "DEADBEEF")
        self.assertEqual(out["pause_vm_method_payload_lengths"]["ChangeState_r"], 13)
        self.assertFalse(out["opaque_operand_is_verified_guest_function"])
        self.assertEqual(out["shared_opcode_0x28_vm_uses"], 1)
        self.assertFalse(out["opaque_operand_is_pause_specific"])
        self.assertFalse(out["guest_ppc_event_handler_abi_verified"])
        self.assertFalse(out["native_windows_pause_menu_binding_installed"])

    def test_common_vm_call_operand_cannot_identify_original_pause_callback(self):
        data = object_fixture()
        # The actual original user archives have hundreds of these VM
        # call operations in entirely unrelated scripts, so a PPC hook
        # cannot be inferred from this number.
        shared = bytes([0x28]) + (0xDEADBEEF).to_bytes(4, "big")
        many = inspect_objects(data + shared * 24, "xbox_360_source")
        self.assertEqual(many["shared_opcode_0x28_vm_uses"], 25)
        self.assertTrue(many["opaque_operand_shared_across_unrelated_scripts"])
        self.assertFalse(many["opaque_operand_is_pause_specific"])
        self.assertFalse(many["guest_ppc_event_handler_abi_verified"])

    def test_cross_platform_shared_method_order_not_hardcoded_offsets(self):
        pc = inspect_objects(object_fixture("little", 0), "pc_2008_comparator")
        xbox = inspect_objects(object_fixture("big", 72), "xbox_360_source")
        self.assertEqual(list(pc["pause_route_offsets_from_page"]),
                         list(xbox["pause_route_offsets_from_page"]))
        self.assertNotEqual(pc["pause_route_offsets_from_page"]["Settings_Page"],
                            xbox["pause_route_offsets_from_page"]["Settings_Page"])
        self.assertEqual(pc["pause_vm_method_payload_lengths"],
                         xbox["pause_vm_method_payload_lengths"])
        self.assertEqual(pc["change_state_vm_opaque_0x28_operand"],
                         xbox["change_state_vm_opaque_0x28_operand"])

    def test_missing_reordered_duplicate_or_missing_init_refused(self):
        source = object_fixture()
        bad = [
            source + b"UIPage_Pause_Settings\0",
            source.replace(b"Settings_Page\0", b"Settings_Xage\0", 1),
            source.replace(b"ChangeState_r\0", b"ChangeState_X\0", 1),
            source.replace(b"uipage_pause_gamesettings_z\0", b"uipage_unknown_gamesettings_z\0", 1),
            source.replace(b"Init_z\0", b"Init_X\0"),
            source.replace(b"\x28\xde\xad\xbe\xef", b"\x29\xde\xad\xbe\xef", 1),
            source.replace((zlib.crc32(b"ChangeState_r".upper()) & 0xffffffff).to_bytes(4, "big"),
                           b"\x00\x00\x00\x00", 1),
        ]
        for data in bad:
            with self.subTest(data=data[-30:]), self.assertRaises(ValueError):
                inspect_objects(data, "xbox_360_source")

    def test_original_action_names_are_only_an_address_level_symbol_pair(self):
        valid = b"prefix\0UI_ActivateOptions\0HUD\0UI_OptionScreen\0"
        out = inspect_actions(valid)
        self.assertEqual(out["original_action_symbol_order"],
                         ["UI_ActivateOptions", "UI_OptionScreen"])
        self.assertFalse(out["action_to_guest_function_abi_verified"])
        with self.assertRaises(ValueError):
            inspect_actions(b"UI_OptionScreen\0UI_ActivateOptions\0")
        with self.assertRaises(ValueError):
            inspect_actions(valid + b"UI_ActivateOptions\0")


if __name__ == "__main__":
    unittest.main()
