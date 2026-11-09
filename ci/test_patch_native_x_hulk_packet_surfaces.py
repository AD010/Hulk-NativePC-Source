#!/usr/bin/env python3
"""Source-level regression for strict per-PM4 RT/depth surface ownership."""
import unittest
from patch_native_x_hulk_packet_surfaces import EDITS,patch_text
class SurfaceTests(unittest.TestCase):
 def test_atomic_and_idempotent(self):
  for path in ("src/state.h","src/state.cpp","src/renderer.cpp"):
   rules=[e for e in EDITS if e["path"]==path]
   src="\n".join(e["old"] for e in rules)
   after,changed=patch_text(src,rules)
   self.assertTrue(changed)
   self.assertEqual(patch_text(after,rules),(after,False))
   with self.assertRaises(ValueError): patch_text("upstream moved",rules)
 def test_packet_binding_is_not_late_shadow(self):
  text="\n".join(e["new"] for e in EDITS)
  for token in ("HulkRegisterAtDraw","HulkSurfaceKey","gpu_color_mask",
                "gpu_depth_control","packet_surfaces","HulkPacketSurfaces",
                "HulkRememberSurface","skipped ambiguous synthetic"):
   self.assertIn(token,text)
 def test_no_changes_to_real_hooked_capture_path(self):
  txt="\n".join(e["new"] for e in EDITS)
  self.assertIn("packet_surfaces ? (*packet_surfaces)[i] : ReadBoundSurface",txt)
  self.assertIn("if (!packet_surfaces) HulkRememberSurface",txt)
if __name__=="__main__": unittest.main()
