#!/usr/bin/env python3
"""Synthetic regression: no copyrighted Hulk functions/XEX/XEXP needed."""
import hashlib
from pathlib import Path
import tempfile
import unittest

from diff_hulk_tu1_guest_ppc import (
    check_exact_eu_patch, read_recompiled_functions, compare,
    normalized_base_signature, match_usa_functions, check_usa_executable,
)

class Tu1WaterEvidenceTests(unittest.TestCase):
    @staticmethod
    def make_root(root, data):
        root.mkdir()
        (root/"hulk_recomp.0.cpp").write_text(data, encoding="utf-8")
        return root


    def test_usa_source_pin_is_mandatory(self):
        with self.assertRaisesRegex(ValueError, "SHA-pinned USA"):
            check_usa_executable(b"XEX2 wrong region or wrong revision")

    def test_internal_branch_topology_not_normalized_away(self):
        # Same instructions with different internal branches are *not*
        # safe equivalent functions, even though target addresses relocate.
        def function(base, target):
            return {
                "count": 26,
                "addresses": tuple(base + i * 4 for i in range(26)),
                "instructions": ("addi r3,r3,1",) * 25
                                + (f"b {base + 4 * target:08X}",),
            }
        eu = function(0x82001000, 0)
        relocated_same = function(0x82301000, 0)
        changed_control_flow = function(0x82301000, 6)
        self.assertEqual(normalized_base_signature(eu),
                         normalized_base_signature(relocated_same))
        self.assertNotEqual(normalized_base_signature(eu),
                            normalized_base_signature(changed_control_flow))
        changed = [{"guest_function": "0x82001000"}]
        candidates = match_usa_functions(
            {0x82001000: eu}, {0x82301000: changed_control_flow}, changed)
        self.assertEqual(candidates[0]["status"], "no_unique_exact_match")
        self.assertIsNone(candidates[0]["usa_candidate"])

        with self.assertRaisesRegex(ValueError, "Invalid PPC instruction"):
            normalized_base_signature({
                "count": 2, "addresses": (0x82001000, 0x82001000),
                "instructions": ("blr", "blr"),
            })

    def test_optional_usa_correlation_is_unique_or_fail_closed(self):
        # EU original and US may have relocated PPC branch target addresses.
        # Only branch destinations may differ, never ordinary gameplay constants.
        eu = {"count": 26, "instructions":
              ("addi r3,r3,1",) * 25 + ("bl 82004000",)}
        us = {"count": 26, "instructions":
              ("addi r3,r3,1",) * 25 + ("bl 82304000",)}
        different = {"count": 26, "instructions":
                     ("addi r3,r3,2",) * 25 + ("bl 82304000",)}
        changed = [{"guest_function": "0x82001000"}]
        eu["addresses"] = tuple(0x82001000 + i * 4 for i in range(26))
        us["addresses"] = tuple(0x82301000 + i * 4 for i in range(26))
        different["addresses"] = us["addresses"]
        self.assertEqual(normalized_base_signature(eu),
                         normalized_base_signature(us))
        self.assertNotEqual(normalized_base_signature(eu),
                            normalized_base_signature(different))
        report = match_usa_functions(
            {0x82001000: eu}, {0x82301000: us, 0x82305000: different}, changed)
        self.assertEqual(report[0]["usa_candidate"], "0x82301000")
        self.assertEqual(report[0]["status"], "unique_normalized_ppc_match")
        self.assertNotIn("addi r3,r3,1", str(report))

        duplicate = match_usa_functions(
            {0x82001000: eu}, {0x82301000: us, 0x82306000: us}, changed)
        self.assertEqual(duplicate[0]["status"], "ambiguous_identical_functions")
        self.assertIsNone(duplicate[0]["usa_candidate"])

        no_match = match_usa_functions(
            {0x82001000: eu}, {0x82305000: different}, changed)
        self.assertEqual(no_match[0]["status"], "no_unique_exact_match")
        self.assertIsNone(no_match[0]["usa_candidate"])

        short = {"count": 1, "instructions": ("blr",)}
        too_short = match_usa_functions(
            {0x82001000: short}, {0x82301000: short}, changed)
        self.assertEqual(too_short[0]["status"], "too_short")

    def test_real_guest_address_chronology_and_diff(self):
        before = (
            "DEFINE_REX_FUNC(sub_82001000)\n"
            "  // lis r3, 100\n"
            "  // addi r3,r3,1\n"
            "loc_82001020:\n"
            "  // bl 82002000\n"
            "DEFINE_REX_FUNC(sub_82002000)\n"
            "  // blr\n"
        )
        updated = (
            "DEFINE_REX_FUNC(sub_82001000)\n"
            "  // lis r3, 100\n"
            "  // addi r3,r3,2\n"
            "loc_82001020:\n"
            "  // bl 82002000\n"
            "DEFINE_REX_FUNC(sub_82002000)\n"
            "  // blr\n"
            "DEFINE_REX_FUNC(sub_82003000)\n"
            "  // blr\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            a,fc=read_recompiled_functions(self.make_root(root/"base",before))
            b,fd=read_recompiled_functions(self.make_root(root/"tu1",updated))
        self.assertEqual((fc,fd),(1,1))
        self.assertEqual(a[0x82001000]["addresses"], (0x82001000,0x82001004,0x82001020))
        diff=compare(a,b)
        self.assertEqual(diff["unchanged_function_count"],1)
        self.assertEqual(diff["changed_function_count"],1)
        self.assertEqual(diff["changed_guest_functions"][0]["guest_first_changed"],"0x82001004")
        self.assertEqual(diff["changed_guest_functions"][0]["instruction_replacements"],1)
        self.assertEqual(diff["added_function_addresses"],["0x82003000"])
        self.assertEqual(diff["removed_function_addresses"],[])
        self.assertNotIn("addi r3,r3,2",str(diff))

    def test_rexglue_block_comments_are_not_instructions(self):
        # Matches the real ReXGlue codegen layout: fallthrough blocks have
        # '// Block:' but may not have a loc_ label.
        source = (
            "DEFINE_REX_FUNC(sub_82001000) {\n"
            "    // Block: 0x82001000\n"
            "    // lis r3, 100\n"
            "    ctx.r3.u32 = 100;\n"
            "    // Block: 0x82001010\n"
            "    // addi r3,r3,2\n"
            "    ctx.r3.u32 += 2;\n"
            "loc_82001040:\n"
            "    // Block: 0x82001040\n"
            "    // blr\n"
            "    return;\n"
            "}\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            found, files = read_recompiled_functions(
                self.make_root(root / "gen", source))
        self.assertEqual(files, 1)
        self.assertEqual(found[0x82001000]["addresses"],
                         (0x82001000, 0x82001010, 0x82001040))
        self.assertEqual(found[0x82001000]["count"], 3)
        self.assertNotIn("Block:", found[0x82001000]["instructions"])

    def test_does_not_silently_overwrite_duplicate_instruction_addresses(self):
        source = (
            "DEFINE_REX_FUNC(sub_82001000) {\n"
            "    // Block: 0x82001000\n"
            "    // lis r3, 100\n"
            "    // Block: 0x82001000\n"
            "    // addi r3,r3,2\n"
            "}\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaisesRegex(ValueError, "Duplicate guest PPC instruction"):
                read_recompiled_functions(self.make_root(root / "gen", source))

    def test_refuses_wrong_european_or_fabricated_patch(self):
        with self.assertRaisesRegex(ValueError,"original EU"):
            check_exact_eu_patch(b"XEX2",b"XEX2")
        with self.assertRaisesRegex(ValueError,"empty"):
            compare({}, {0x82001000:{"sha256":"a"}})

    def test_unknown_assembly_root_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError,"No ReXGlue"):
                read_recompiled_functions(Path(tmp))

    def test_duplicate_ppc_function_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/"hulk_recomp.0.cpp"
            p.write_text("DEFINE_REX_FUNC(sub_82001000)\n // blr\n"
                         "DEFINE_REX_FUNC(sub_82001000)\n // blr\n",encoding="utf-8")
            with self.assertRaisesRegex(ValueError,"Duplicate"):
                read_recompiled_functions(Path(tmp))

if __name__=="__main__":
    unittest.main()
