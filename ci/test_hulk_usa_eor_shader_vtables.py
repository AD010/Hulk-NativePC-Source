#!/usr/bin/env python3
"""Source-only authentic EoR shader virtual method correlation regression.

No original Xbox executable/VM instructions, GPU buffers or materials needed.
The test NEVER promotes a method identity to a VerifiedXdkResourceWitness.
"""
from pathlib import Path
from configparser import ConfigParser
import csv
import json
import re
import unittest
from hulk_usa_eor_shader_vtables import (
    HIGHLIGHTS, USA_MAPPED_PPC_SHA256,
)

ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT / "native_port"
WITNESS = PORT / "analysis/hulk_usa_eor_shader_vtables_2026-11-21.json"
EXPECTED_NATIVE = {
    0x8265B420: 0x0014025EDC0,
    0x82668D50: 0x001419B0F10,
    0x82669630: 0x00141574F00,
    0x8266A0C0: 0x00142BE0180,
    0x8266CC28: 0x00140D9E2D0,
    0x8266DDD0: 0x001421CC520,
}


class OriginalEorShaderClassesTests(unittest.TestCase):
    def test_full_original_vtable_names_and_guest_entry_identity(self):
        report = json.loads(WITNESS.read_text(encoding="utf-8"))
        self.assertEqual(report["original_mapped_guest_image_sha256"],
                         USA_MAPPED_PPC_SHA256)
        entries = report["original_shader_vtables"]
        self.assertEqual(report["vtable_class_count"], 52)
        self.assertEqual(len(entries), 52)
        self.assertEqual(len({e["original_shader_class_name"] for e in entries}), 52)
        self.assertEqual(len({e["original_guest_vtable"] for e in entries}), 52)
        by_name = {e["original_shader_class_name"]: e for e in entries}
        for name, (vtable, source_method) in HIGHLIGHTS.items():
            entry = by_name[name]
            self.assertEqual(int(entry["original_guest_vtable"], 16), vtable)
            self.assertEqual(int(entry["original_guest_method_slot_4"], 16),
                             source_method)
            self.assertEqual(int(entry["original_class_name_record"], 16),
                             vtable + 56)
            self.assertTrue(entry["original_shader_vtable_source_verified"])
            self.assertFalse(entry["original_effect_material_sampler_abi_verified"])
        self.assertIn("EBuildingWithWindowsShader", by_name)
        self.assertIn("ESkinnedMultiMatEnvPartialEmissiveScrollShader", by_name)
        self.assertFalse(report["original_runtime_eor_model_material_abi_verified"])
        self.assertFalse(report["original_pause_ui_event_dispatch_abi_verified"])
        self.assertTrue(report["all_guest_code_and_original_assets_remain_private"])

    def test_six_source_engine_hooks_match_original_AOT_guest_to_host(self):
        with (PORT / "analysis/guest_host_mapping.csv").open(encoding="utf-8-sig",
                                                             newline="") as fp:
            rows = list(csv.DictReader(fp))
        registry = {int(r["guest_address"], 16): int(r["native_entry"], 16)
                    for r in rows}
        hooks = {int(line.split("#",1)[0].strip(), 16)
                 for line in (PORT / "native_hook_addresses.txt").read_text(
                     encoding="utf-8").splitlines()
                 if line.split("#",1)[0].strip()}
        cfg = ConfigParser(interpolation=None)
        self.assertTrue(cfg.read(PORT / "hulk_native_x_profile.ini"))
        xbox_profile = {int(key, 16) for key in cfg["hooks"]}
        self.assertTrue(set(EXPECTED_NATIVE).issubset(hooks))
        self.assertTrue(set(EXPECTED_NATIVE).isdisjoint(xbox_profile))
        for original_guest, native in EXPECTED_NATIVE.items():
            self.assertEqual(registry[original_guest], native)
        generator = (PORT / "tools/gen_native_x_hooks.py").read_text(
            encoding="utf-8")
        self.assertIn("HULK_EOR_SHADER_TRACE_HOOK", generator)
        self.assertIn("ScopedOriginalEorShaderMethod", generator)
        self.assertIn("if addr in SOURCE_EOR_SHADER_METHODS", generator)

    def test_original_engine_method_observer_is_read_only_and_fail_closed(self):
        header = (PORT / "src/eor_xdk_draw_capture.h").read_text(
            encoding="utf-8")
        bridge = (PORT / "src/eor_xdk_hook_bridge.cpp").read_text(
            encoding="utf-8")
        for name, (_, method) in HIGHLIGHTS.items():
            self.assertIn("case 0x%08Xu:" % method, header, name)
        self.assertIn("ProvenUsaEorShaderVtableForMethod", header)
        self.assertIn("g_source_shader_method_stack", bridge)
        self.assertIn("ScopedOriginalEorShaderMethod::~ScopedOriginalEorShaderMethod", bridge)
        self.assertIn("g_source_shader_rejected_nesting", bridge)
        self.assertIn("actual_vtable!=expected_vtable", bridge)
        self.assertIn("ReadGuest(original_guest_base,original_ctx.r3.u32", bridge)
        self.assertIn("draw.original_engine_shader_method=", bridge)
        self.assertIn("g_source_shader_method_stack[g_source_shader_method_depth-1]", bridge)
        self.assertIn("original_shader_method_vtable_verified=true", bridge)
        self.assertNotIn("engine_draw_verified=true", bridge)
        self.assertNotIn("original_guest_resource_to_material_mapping_verified=true",
                         bridge)
        self.assertNotIn("InstallVerifiedWorkerEorFrame(", bridge[
            bridge.index("ScopedOriginalEorShaderMethod::ScopedOriginalEorShaderMethod"):
            bridge.index("// Original USA XDK 6534 resource object")
        ])


if __name__ == "__main__":
    unittest.main()
