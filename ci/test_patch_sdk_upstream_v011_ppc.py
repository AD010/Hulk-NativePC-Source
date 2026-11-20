#!/usr/bin/env python3
"""Source-only upstream v0.11 PPC backport regression tests; no game inputs."""
from __future__ import annotations
from pathlib import Path
import sys
import tempfile
import unittest
from patch_sdk_upstream_v011_ppc import (
    FLOAT, LOGICAL, VECTOR, SourceDrift, backport_fctid, backport_shifts,
    backport_vcmpbfp, inspect_source, undo_complete, verify_sdk_head,
)

FLOAT_FIXTURE = '''bool build_fctid(BuilderContext& ctx) {
  ctx.println("({1}.f64 > double(LLONG_MAX)) ? LLONG_MAX : ");
}
bool build_fctidz(BuilderContext& ctx) {
  ctx.println("({1}.f64 > double(LLONG_MAX)) ? LLONG_MAX : ");
}
'''
LOGICAL_FIXTURE = '''bool build_srad(BuilderContext& ctx) {
  ctx.println("\\t{}.s64 = {}.s64 >> {}.u64;", ctx.r(ctx.insn.operands[0]),
              ctx.r(ctx.insn.operands[1]), ctx.temp());
}
bool build_sradi(BuilderContext& ctx) { return true; }
bool build_sraw(BuilderContext& ctx) {
  ctx.println("\\t{}.s64 = {}.s32 >> {}.u32;", ctx.r(ctx.insn.operands[0]),
              ctx.r(ctx.insn.operands[1]), ctx.temp());
}
bool build_srawi(BuilderContext& ctx) { return true; }
'''
VECTOR_FIXTURE = '''bool build_vcmpbfp(BuilderContext& ctx) {
  ctx.println("\\tsimde_mm_and_ps(simde_mm_cmpgt_ps(...");
  ctx.println("\\tsimde_mm_and_ps(simde_mm_cmplt_ps(...");
  if (isRecordForm(ctx.insn))
    ctx.println("x", ctx.cr(6), vD, vD);
  return true;
}
bool build_vcmpeqfp(BuilderContext& ctx) { return true; }
'''

class UpstreamPpcTests(unittest.TestCase):
    def test_fctid_and_fctidz_saturate_2_pow_63(self):
        s = backport_fctid(FLOAT_FIXTURE)
        self.assertEqual(s.count("({1}.f64 >= 0x1p63) ? LLONG_MAX"), 2)
        self.assertEqual(undo_complete(FLOAT, s), FLOAT_FIXTURE)
        self.assertFalse(float(2**63) > float(2**63 - 1))
        self.assertTrue(float(2**63) >= float.fromhex("0x1p63"))

    def test_large_arithmetic_shifts_set_carry(self):
        s = backport_shifts(LOGICAL_FIXTURE)
        self.assertIn("u8 & 0x40", s)
        self.assertIn("u8 & 0x20", s)
        self.assertEqual(undo_complete(LOGICAL, s), LOGICAL_FIXTURE)
        self.assertTrue(all(n & 0x40 for n in (64,65,100,127)))
        self.assertTrue(all(n & 0x20 for n in (32,33,40,63)))

    def test_vector_nan_and_cr6(self):
        s = backport_vcmpbfp(VECTOR_FIXTURE)
        self.assertIn("simde_mm_andnot_ps(simde_mm_cmple_ps", s)
        self.assertIn("simde_mm_andnot_ps(simde_mm_cmpge_ps", s)
        self.assertIn('ctx.println("\\t{}.lt = 0;", ctx.cr(6));', s)
        self.assertEqual(undo_complete(VECTOR, s), VECTOR_FIXTURE)
        nan = float("nan")
        self.assertEqual((0x80000000 if not nan <= 1 else 0) |
                         (0x40000000 if not nan >= -1 else 0), 0xC0000000)

    def test_idempotent_preflight_and_partial_rejection(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            for path, txt in ((FLOAT,FLOAT_FIXTURE),(LOGICAL,LOGICAL_FIXTURE),(VECTOR,VECTOR_FIXTURE)):
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(txt, encoding="utf-8")
            prepared = inspect_source(root)
            for path, txt in prepared.items():
                (root / path).write_text(txt, encoding="utf-8")
            self.assertEqual(inspect_source(root), prepared)
            (root / VECTOR).write_text(VECTOR_FIXTURE.replace(
                "simde_mm_and_ps(simde_mm_cmpgt_ps",
                "simde_mm_andnot_ps(simde_mm_cmple_ps"), encoding="utf-8")
            with self.assertRaises(SourceDrift):
                inspect_source(root)

    def test_unknown_abi_fails_closed(self):
        with self.assertRaises(SourceDrift):
            backport_fctid(FLOAT_FIXTURE.replace("LLONG_MAX", "OTHER"))
        with self.assertRaises(SourceDrift):
            backport_shifts(LOGICAL_FIXTURE.replace("build_srawi", "unknown"))
        with self.assertRaises(SourceDrift):
            backport_vcmpbfp(VECTOR_FIXTURE.replace("ctx.cr(6)", "ctx.cr(7)"))

if __name__ == "__main__":
    sdk = Path(sys.argv.pop(1)) if len(sys.argv) > 1 else None
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(UpstreamPpcTests))
    if not result.wasSuccessful():
        raise SystemExit(1)
    if sdk:
        verify_sdk_head(sdk)
        assert len(inspect_source(sdk)) == 3
        print("[HULK/SDK-0.11] pinned fork builder source anchors passed")
