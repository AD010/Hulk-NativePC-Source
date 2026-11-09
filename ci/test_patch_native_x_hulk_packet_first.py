#!/usr/bin/env python3
"""Synthetic regression tests: object-less PM4 draw must reach the GPU gate."""
import unittest
from patch_native_x_hulk_packet_first import OLD_GUARD,NEW_GUARD,OLD_EXACT,NEW_EXACT,patch_text

class TestPacketFirst(unittest.TestCase):
 def test_independent_and_idempotent(self):
  old=OLD_GUARD+"\n"+OLD_EXACT
  new,changed=patch_text(old)
  self.assertTrue(changed)
  self.assertEqual(patch_text(new),(new,False))
  with self.assertRaises(ValueError):patch_text("wrong renderer source")
  with self.assertRaises(ValueError):patch_text(NEW_GUARD+"\n"+OLD_EXACT)
 def test_known_gpu_surface_not_preemptively_rejected(self):
  self.assertNotIn("if (!has_surface)",NEW_GUARD)
  self.assertNotIn("ReadBoundSurface(base, device, i)",NEW_GUARD)
  self.assertIn("HulkPacketSurfaces(tracker, base, device, packet_surfaces)",NEW_EXACT)
  self.assertIn("CaptureDraw(synthetic, tracker, base, &packet_surfaces)",NEW_EXACT)
  self.assertIn("HULK/PM4-REGISTER-ONLY",NEW_EXACT)
 def test_integrated_anchor_with_added_cpp_comment(self):
  # Synthetic-only tests are not sufficient; the CI preflight also applies all
  # patches to the real upstream SHA. This protects tolerable comment drift.
  source=OLD_GUARD + "\n" + OLD_EXACT
  out,changed=patch_text(source)
  self.assertTrue(changed)
  self.assertIn("HULK/PM4-REGISTER-ONLY",out)

if __name__=="__main__": unittest.main()
