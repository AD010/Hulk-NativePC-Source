#!/usr/bin/env python3
"""Check getCompTexLOD stage safety and preserved native shader query."""
import unittest
from patch_sdk_hulk_computed_lod import patch,ORIGINAL,FIXED,VERSION_OLD,VERSION_NEW

VALID=ORIGINAL + "if (instr.opcode == ucode::FetchOpcode::kGetTextureComputedLod) {\n" \
     + " result[0] = QueryTextureLod(texture_parameters, image, sampler);\n}\n" \
     + "spv::OpImageQueryLod\n"

class TestComputedLOD(unittest.TestCase):
    def test_fragment_query_not_replaced_with_zero(self):
        shader,header,modified=patch(VALID,VERSION_OLD)
        self.assertTrue(modified)
        self.assertEqual(patch(shader,header),(shader,header,False))
        self.assertIn("HULK/COMP-LOD-NATIVE",shader)
        self.assertIn("use_computed_lod = true",shader)
        self.assertIn("QueryTextureLod(texture_parameters,",shader)
        assert header == VERSION_NEW
        fragment=shader[shader.index("if (is_pixel_shader()) {"):
                        shader.index("} else {",shader.index("if (is_pixel_shader()) {"))]
        self.assertNotIn("StoreResult(",fragment)
        self.assertIn("StoreResult(",shader[shader.index("} else {",shader.index("if (is_pixel_shader()) {")):])
    def test_wrong_source_and_half_patches_refused(self):
        for s,h in (("garbled",VERSION_OLD),(ORIGINAL,VERSION_OLD),
                    (VALID,VERSION_NEW),(FIXED,VERSION_OLD)):
            with self.assertRaises(ValueError):patch(s,h)
    def test_version_after_exponent_patch(self):
        self.assertIn("kVersion = 13;",VERSION_OLD)
        self.assertIn("kVersion = 14;",VERSION_NEW)

if __name__=="__main__":
    unittest.main()
