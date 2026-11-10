#!/usr/bin/env python3
"""Regression: Vulkan PM4 fallback is bounded to real bound surfaces."""
import unittest
from patch_native_x_hulk_surface_fallback import CVAR,DECL_ANCHOR,DRAW_OLD,DRAW_NEW,patch_text

class RecoveryTest(unittest.TestCase):
 def test_exact_and_idempotent(self):
  before=DECL_ANCHOR+"\n"+DRAW_OLD
  after,changed=patch_text(before)
  self.assertTrue(changed)
  self.assertEqual((after,False),patch_text(after))
  with self.assertRaises(ValueError): patch_text("moved native source")
  with self.assertRaises(ValueError): patch_text(CVAR+"\n"+DRAW_OLD)
 def test_exact_packet_before_fallback(self):
  self.assertLess(DRAW_NEW.index("if (HulkPacketSurfaces("),DRAW_NEW.index("else if (REXCVAR_GET("))
  self.assertIn("CaptureDraw(synthetic, tracker, base, &packet_surfaces);",DRAW_NEW)
  self.assertIn("CaptureDraw(synthetic, tracker, base);",DRAW_NEW)
 def test_requires_authentic_bound_object(self):
  self.assertIn("ReadBoundSurface(base, device, slot)",DRAW_NEW)
  self.assertIn("if (!has_bound_surface) return;",DRAW_NEW)
  self.assertIn("native_x_renderer_hulk_surface_fallback",CVAR)
  self.assertIn("native_x_renderer_hulk_surface_fallback, false",CVAR)
if __name__=="__main__":unittest.main()
