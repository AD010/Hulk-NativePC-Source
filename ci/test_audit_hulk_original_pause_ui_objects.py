#!/usr/bin/env python3
"""Synthetic original EoR UI VM route tests; no proprietary archive in source."""
import unittest
from audit_hulk_original_pause_ui_objects import (
    PAUSE_SEQUENCE, inspect_actions, inspect_objects,
)


def object_fixture(extra_after_options=0):
    parts = []
    for name in PAUSE_SEQUENCE:
        parts.append(name.encode("ascii") + b"\0")
        if name == "OptionsMenuState":
            parts.append(b"\x01" * extra_after_options)
        else:
            parts.append(b"\x01" * 16)
    parts.append(b"\x02" * 32)
    parts.append(b"uipage_pause_gamesettings_z\0")
    parts.append(b"\x03" * 40)
    parts.append(b"Init_z\0")
    return b"".join(parts)


class OriginalPauseScriptTests(unittest.TestCase):
    def test_source_route_and_game_settings_init(self):
        data = object_fixture()
        out = inspect_objects(data, "synthetic")
        self.assertEqual(list(out["pause_route_offsets_from_page"]), list(PAUSE_SEQUENCE))
        self.assertTrue(out["game_settings_script_present"])
        self.assertGreater(out["game_settings_init_relative"], 0)
        self.assertFalse(out["guest_ppc_event_handler_abi_verified"])
        self.assertFalse(out["native_windows_pause_menu_binding_installed"])

    def test_cross_platform_shared_method_order_not_hardcoded_offsets(self):
        pc = inspect_objects(object_fixture(0), "pc")
        xbox = inspect_objects(object_fixture(72), "xbox")
        self.assertEqual(list(pc["pause_route_offsets_from_page"]),
                         list(xbox["pause_route_offsets_from_page"]))
        self.assertNotEqual(pc["pause_route_offsets_from_page"]["Settings_Page"],
                            xbox["pause_route_offsets_from_page"]["Settings_Page"])

    def test_missing_reordered_duplicate_or_missing_init_refused(self):
        source = object_fixture()
        bad = [
            source + b"UIPage_Pause_Settings\0",
            source.replace(b"Settings_Page\0", b"Settings_Xage\0", 1),
            source.replace(b"ChangeState_r\0", b"ChangeState_X\0", 1),
            source.replace(b"uipage_pause_gamesettings_z\0", b"uipage_unknown_gamesettings_z\0", 1),
            source.replace(b"Init_z\0", b"Init_X\0"),
        ]
        for data in bad:
            with self.subTest(data=data[-30:]), self.assertRaises(ValueError):
                inspect_objects(data, "bad")

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
