import unittest
from patch_hulk_tu1_ledge_region import patch, MARKER
SOURCE='''DEFINE_REX_FUNC(sub_82288580) {
  // Block: 0x822885A0
  // bl 0x826bd808
  sub_826BD808(ctx, base);
  // li r5,1
  // addi r3,r1,112
  // lwz r4,164(r31)
  // lfs f30,272(r1)
  temp.u32 = REX_LOAD_U32(ctx.r1.u32 + 272);
  f30.f64 = double(temp.f32);
  // lfs f29,276(r1)
  temp.u32 = REX_LOAD_U32(ctx.r1.u32 + 276);
  f29.f64 = double(temp.f32);
  // lfs f28,280(r1)
  temp.u32 = REX_LOAD_U32(ctx.r1.u32 + 280);
  f28.f64 = double(temp.f32);
  // bl 0x825f6938
  sub_825F6938(ctx, base);
  // Block: 0x8228870C
  // li r3,0
  ctx.r3.s64 = 0;
  // b 0x822887c8
  goto loc_822887C8;
loc_822887C8:
  // addi r1,r1,384
  ctx.r1.u64 += 384;
}
'''
class LedgeRegion(unittest.TestCase):
    def test_install(self):
        s=patch(SOURCE)
        self.assertIn('f29.f64 < -30.0 && f30.f64 > -3400.0 && f30.f64 < -2600.0 && f28.f64 > 1950.0 && f28.f64 < 2450.0',s)
        self.assertIn('ctx.r3.s64 = 0; goto loc_822887C8;',s)
        self.assertEqual(patch(s),s)
        self.assertEqual(s.count(MARKER),1)
    def test_reject(self):
        for s in (SOURCE+SOURCE,SOURCE.replace(' + 276',' + 280'),SOURCE.replace('f28.f64 =','f27.f64 ='),SOURCE.replace('loc_822887C8:', 'loc_822887D0:'),SOURCE.replace('bl 0x825f6938','bl 0x825f693c')):
            with self.assertRaises(ValueError):patch(s)
    def test_macro_variant(self):
        s=patch(SOURCE.replace('REX_LOAD_U32','PPC_LOAD_U32').replace('f29.f64','ctx.f29.f64'))
        self.assertIn('ctx.f29.f64 < -30.0',s)
if __name__=='__main__':unittest.main()
