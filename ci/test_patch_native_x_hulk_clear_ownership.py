#!/usr/bin/env python3
"""Source-level regression for color/depth clear content ownership."""
import unittest
from patch_native_x_hulk_clear_ownership import PATCHES, patch_text
class ClearOwnershipTests(unittest.TestCase):
 def test_atomic_and_idempotent(self):
  before="\n".join(r["old"] for r in PATCHES)
  after,changed=patch_text(before)
  self.assertTrue(changed)
  self.assertEqual(patch_text(after),(after,False))
  with self.assertRaises(ValueError):patch_text("upstream moved")
  with self.assertRaises(ValueError):
   patch_text(PATCHES[0]["new"]+"\n"+PATCHES[1]["old"])
 def test_written_only_when_clear_was_issued(self):
  for r in PATCHES:
   self.assertIn("bool wrote = false;",r["new"])
   self.assertIn("if (wrote) {\n    target->written = ++g_target_writes;",r["new"])
   self.assertEqual(r["new"].count("wrote = true;"),2)
  self.assertIn("if (!depth && !stencil) return;",PATCHES[1]["new"])
if __name__=="__main__": unittest.main()
