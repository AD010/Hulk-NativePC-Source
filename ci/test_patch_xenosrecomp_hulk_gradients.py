#!/usr/bin/env python3
"""Hulk source-only Xenos registered-gradient patch regression."""
import unittest
from patch_xenosrecomp_hulk_gradients import (
    patch_sources, GATE, LAMBDA_END, LAMBDA_NEW,
    DIM_OLD, DIM_NEW, OFFSET_OLD, DECLARE_OLD, EMIT_GRAD,
    HPP_OLD, HELPER_ANCHOR, MARK)
from patch_xenosrecomp_hulk_lod import LODGATE, EMIT_NEW

class GradientTests(unittest.TestCase):
    def fixture(self):
        return ('\n'.join([LODGATE,LAMBDA_END,DIM_OLD,OFFSET_OLD,
                           DECLARE_OLD,EMIT_NEW]), HPP_OLD, HELPER_ANCHOR)

    def test_native_explicit_2d_fetch(self):
        cpp,hpp,common=self.fixture()
        c,h,s,changed=patch_sources(cpp,hpp,common)
        self.assertTrue(changed)
        self.assertEqual(patch_sources(c,h,s),(c,h,s,False))
        self.assertIn(GATE,c)
        self.assertIn(LAMBDA_NEW,c)
        self.assertIn(DIM_NEW,c)
        self.assertIn('hulkGradH.xy, hulkGradV.xy',c)
        self.assertIn('hulk_grad_h_set && hulk_grad_v_set',c)
        self.assertIn('instr.isPredicated',c)
        self.assertIn('!instr.useRegLod',c)
        self.assertIn('SampleGrad(',s)
        self.assertIn('hulk_grad_h_set = false',h)
        self.assertIn(MARK,c)
        self.assertIn(EMIT_GRAD,c)

    def test_partially_patched_or_unknown_upstream_fail_closed(self):
        cpp,hpp,common=self.fixture()
        for triple in ((cpp.replace(LODGATE,''),hpp,common),
                       (cpp,hpp.replace(HPP_OLD,''),common),
                       (cpp,hpp,common.replace(HELPER_ANCHOR,'')),
                       (cpp+MARK,hpp,common)):
            with self.assertRaises(ValueError):patch_sources(*triple)

if __name__=='__main__':
    unittest.main()
