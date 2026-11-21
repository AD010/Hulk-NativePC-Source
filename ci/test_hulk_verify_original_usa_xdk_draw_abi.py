#!/usr/bin/env python3
"""No proprietary game code: synthetic original USA XDK PPC provenance tests."""
import json
from pathlib import Path
import unittest

from hulk_verify_original_usa_xdk_draw_abi import (
    SPECS, USA_XEX_SHA256, inspect,
)

ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "native_port/analysis/hulk_usa_xdk_boundary_witness_2026-11-21.json"


def synthetic_evidence():
    candidates = {}
    for name, spec in SPECS.items():
        guest = "0x%08X" % spec["guest"]
        # Generic synthetic PPC register copies; no source program bytes.
        copy_rows = [
            {"asm": "mr r%d,r%d" % (target, 3 + i)}
            for i, target in enumerate(spec["copies"])
        ]
        candidates[guest] = {
            "first_ppc_instructions": copy_rows + [
                {"asm": "ld r4,0(r31)"}
            ],
            "direct_callers": [
                {"caller_guest": "0x%08X" % caller,
                 "site": "0x%08X" % site,
                 "kind": kind}
                for caller, site, kind in spec["calls"]
            ],
        }
    return {
        "schema": "hulk_native_xdk_6534_evidence_v1",
        "xex_sha256": USA_XEX_SHA256,
        "guest_function_definitions_scanned": 98726,
        "candidates": candidates,
    }


class OriginalUsaPpcDrawAbiTests(unittest.TestCase):
    def test_eight_original_xdk_direct_branches_are_distinct(self):
        result = inspect(synthetic_evidence())
        draws = result["draw_xdk_abis"]
        self.assertEqual(set(draws), {"DrawIndexedVertices", "DrawVertices"})
        self.assertEqual({name: len(v["source_defined_direct_callers"])
                          for name, v in draws.items()},
                         {"DrawIndexedVertices": 6, "DrawVertices": 2})
        for name, data in draws.items():
            self.assertEqual(
                len(data["source_verified_original_ppc_argument_registers"]),
                len(data["xdk_method_argument_roles"]))
            for row in data["source_defined_direct_callers"]:
                site = int(row["static_direct_branch_site"], 16)
                if row["powerpc_branch_kind"] == "bl":
                    self.assertEqual(int(row["expected_xdk_lr"], 16), site + 4)
                else:
                    self.assertIsNone(row["expected_xdk_lr"])
                self.assertFalse(row["original_engine_mesh_material_abi_verified"])
            self.assertFalse(data["original_engine_material_draw_handler_verified"])
        self.assertFalse(result["original_pause_settings_ppc_dispatcher_abi_verified"])
        self.assertFalse(result["original_eor_engine_model_mesh_material_shader_abi_verified"])
        self.assertFalse(result["full_eor_vulkan_frame_draws_verified"])

    def test_safe_public_source_witness_agrees_with_private_verifier(self):
        public = json.loads(PUBLIC.read_text(encoding="utf-8"))
        self.assertEqual(inspect(synthetic_evidence()), public)
        self.assertTrue(public["no_original_guest_instructions_or_private_game_bytes"])

    def test_usa_hash_and_actual_ppc_register_abi_must_match(self):
        for broken in (
            lambda r: r.update(xex_sha256="0" * 64),
            lambda r: r.update(guest_function_definitions_scanned=1),
            lambda r: r["candidates"].pop("0x8288E318"),
            lambda r: r["candidates"]["0x8288E318"]
                ["first_ppc_instructions"].pop(2),
            lambda r: r["candidates"]["0x8288DF30"]
                ["first_ppc_instructions"][-1].update(asm="nop"),
            lambda r: r["candidates"]["0x8288E318"]["direct_callers"][0]
                .update(site="0x82640614"),
            lambda r: r["candidates"]["0x8288DF30"]["direct_callers"][0]
                .update(kind="bl"),
        ):
            with self.subTest(broken=repr(broken)):
                evidence = synthetic_evidence()
                broken(evidence)
                with self.assertRaises(ValueError):
                    inspect(evidence)


if __name__ == "__main__":
    unittest.main()
