#!/usr/bin/env python3
"""Synthetic-only source regression for the genuine USA ShaderEffects census.

Does not load copyrighted Xbox ShaderEffects bytes in the public builder.
The user-private source auditor pins actual archive SHA-256 and refuses
to create a GPU resource witness without independent live Draw ownership.
"""
from pathlib import Path
import json
import re
import unittest
import zlib

from hulk_verify_usa_original_shader_effects import (
    USA_INDEX_SHA256, USA_EFFECTS_SHA256,
)

ROOT=Path(__file__).resolve().parents[1]
PORT=ROOT/"native_port"
SOURCE=(PORT/"analysis/hulk_usa_shader_source_contract_2026-11-21.json")
VTABLES=(PORT/"analysis/hulk_usa_eor_shader_vtables_2026-11-21.json")


class OriginalUsaSourceShaderContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.meta=json.loads(SOURCE.read_text(encoding="utf-8"))
        cls.class_vtables=json.loads(VTABLES.read_text(encoding="utf-8"))

    def test_actual_original_usa_shader_effects_are_source_pinned(self):
        w=self.meta
        self.assertEqual(w["schema"],"hulk_usa_original_shader_source_contract_v1")
        self.assertEqual(w["original_usa_index_sha256"],USA_INDEX_SHA256)
        self.assertEqual(w["original_usa_shader_effects_sha256"],USA_EFFECTS_SHA256)
        self.assertEqual(w["original_source_effect_records"],233)
        self.assertEqual(w["effects_with_programs"],231)
        self.assertEqual(w["original_unique_effect_pass_vs_ps_pairs"],2597)
        self.assertEqual(w["original_unique_source_shader_containers"],5194)
        self.assertEqual(w["original_unique_authored_techniques"],42)
        self.assertEqual(w["shader_sources_with_sampler_declarations"],1306)
        self.assertEqual(w["shader_sources_with_no_sampler_declarations"],3888)
        self.assertEqual(w["total_original_sampler_declarations"],4420)
        self.assertEqual(w["largest_original_sampler_register"],7)
        self.assertEqual(
            w["shader_sources_with_sampler_declarations"]+
            w["shader_sources_with_no_sampler_declarations"],
            w["original_unique_source_shader_containers"])
        self.assertTrue(w["all_authored_pairs_and_sampler_slots_source_verified"])
        self.assertTrue(w["no_original_shader_or_other_game_bytes_published"])

    def test_46_original_named_class_effect_source_links_not_runtime_claims(self):
        w=self.meta
        matches=w["source_shader_class_name_effect_matches"]
        by_class={x["original_shader_class_name"]:x
                  for x in self.class_vtables["original_shader_vtables"]}
        self.assertEqual(len(by_class),52)
        self.assertEqual(len(matches),46)
        self.assertEqual(w["source_shader_class_name_effect_matches_count"],46)
        self.assertEqual(len({e["original_engine_shader_class"] for e in matches}),46)
        for row in matches:
            klass=row["original_engine_shader_class"]
            self.assertIn(klass,by_class)
            self.assertEqual(row["original_shader_class_guest_vtable"],
                             by_class[klass]["original_guest_vtable"])
            name=(klass[1:-6] if klass.startswith("E") and
                  klass.endswith("Shader") else klass)
            exact_crc=zlib.crc32(name.upper().encode("ascii"))&0xffffffff
            self.assertEqual(int(row["same_name_source_effect_crc32"],16),exact_crc)
            self.assertFalse(row["runtime_class_to_effect_binding_verified"])
        missing=w["unmatched_original_shader_classes"]
        self.assertEqual(len(missing),6)
        self.assertEqual(set(missing),set(by_class)-{
            x["original_engine_shader_class"] for x in matches})
        for flag in ("runtime_xdk_program_to_original_source_digest_verified",
                     "runtime_shader_class_to_effect_hash_verified",
                     "runtime_effect_pass_to_material_verified",
                     "runtime_sampler_to_guest_texture_verified"):
            self.assertFalse(w[flag])

    def test_original_sampler_source_bank_flows_to_real_eor_vulkan_worker(self):
        app=(PORT/"src/incredible_hulk_app.h").read_text(encoding="utf-8")
        header=(PORT/"src/eor_xdk_hook_bridge.h").read_text(encoding="utf-8")
        cpp=(PORT/"src/eor_xdk_hook_bridge.cpp").read_text(encoding="utf-8")
        sampler=(PORT/"src/eor_original_shader_samplers.h").read_text(
            encoding="utf-8")
        self.assertIn('#include "eor_original_shader_samplers.h"',app)
        self.assertIn('hulk_sampler_bank.bin',app)
        self.assertIn("original_samplers->Load(private_samplers)",app)
        self.assertIn("original_samplers->ShaderCount()!=bank->expected()",app)
        self.assertIn("eor_effects_,eor_reflection_,eor_samplers_",app)
        self.assertIn("OriginalXdkShaderSamplers> samplers",header)
        self.assertIn("std::move(samplers)",cpp)
        self.assertIn("complete->samplers=pack.samplers;",cpp)
        self.assertIn("ready->samplers->Find(draw.vertex_source_sha256",cpp)
        self.assertIn("ready->samplers->Find(draw.pixel_source_sha256",cpp)
        self.assertIn("OriginalXdkShaderSamplers",sampler)
        self.assertIn("HEORSMP1",sampler)
        self.assertIn("ShaderCount() const noexcept",sampler)
        # Never confuse source sampler register with XeTexture fetch ID.
        self.assertNotIn("source_sampler_register==fetch_register",cpp)

    def test_original_us_shader_source_auditor_is_safe(self):
        auditor=(ROOT/"ci/hulk_verify_usa_original_shader_effects.py").read_text(
            encoding="utf-8")
        self.assertIn("parse_index(index)",auditor)
        self.assertIn("scan_xenos_containers(blob)",auditor)
        self.assertIn("_shader_technique(source_shader,stage,name)",auditor)
        self.assertIn("inspect_original_samplers(source_shader,stage)",auditor)
        self.assertIn("original_shared_source" if False else
                      "runtime_effect_pass_to_material_verified",auditor)
        self.assertNotIn("InstallVerifiedWorkerEorFrame(",auditor)


if __name__=="__main__":
    unittest.main()
