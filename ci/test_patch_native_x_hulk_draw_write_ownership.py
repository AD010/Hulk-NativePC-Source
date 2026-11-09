#!/usr/bin/env python3
"""NativeX Vulkan Draw only owns images actually and legally written."""
import unittest
from patch_native_x_hulk_draw_write_ownership import PATCHES, transform

class DrawOwnership(unittest.TestCase):
 def test_source_anchors_idempotence(self):
  for rel,rules in PATCHES.items():
   before="\n".join(old for old,new in rules)
   after,changed=transform(before,rules)
   self.assertTrue(changed,rel)
   self.assertEqual(transform(after,rules),(after,False))
   with self.assertRaises(ValueError):transform("renderer moved",rules)
 def test_result_propagates(self):
  h="\n".join(new for old,new in PATCHES["src/draw.h"])
  cpp="\n".join(new for old,new in PATCHES["src/draw.cpp"])
  self.assertIn("bool Draw(",h)
  self.assertIn("bool DrawPath::Draw(",cpp)
  self.assertIn("return submitted;",cpp)
 def test_masks_only_real_writes(self):
  out="\n".join(new for old,new in PATCHES["src/renderer.cpp"])
  self.assertIn("if (submitted)",out)
  self.assertIn("(mask >> (4 * i)) & 0xFu",out)
  self.assertIn("(depth & 6u) == 6u",out)
  self.assertNotIn("if (targets[i]) targets[i]->written",out)
  for mask,depth,count in ((0,0,0),(0xF,0,1),(0x10,0,1),(0,2,0),(0,6,1),(0,1,1)):
   written=sum(bool((mask>>(4*i))&0xF) for i in range(4))+bool(depth&1 or (depth&6)==6)
   self.assertEqual(written,count)
if __name__=="__main__":unittest.main()
