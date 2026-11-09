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
# Xenos RB_DEPTHCONTROL: bit0=stencil_enable, bit1=z_enable,
# bit2=z_write_enable. A z-only draw MUST keep its depth attachment.
class XenosDepthLayoutTests(unittest.TestCase):
 def test_depth_or_stencil_gate(self):
  source="\n".join(e["new"] for e in EDITS)
  self.assertIn("gpu_depth_control & 0x3u",source)
  self.assertNotIn("gpu_depth_control & 1u",source)
  def needs_depth(bits): return bool(bits & 0x3)
  for value,expected in ((0,False),(1,True),(2,True),(3,True),(4,False),(6,True)):
   with self.subTest(bits=value): self.assertEqual(needs_depth(value),expected)
if __name__=="__main__": unittest.main()
