import unittest
from patch_hulk_tu1_water_exit import patch

SOURCE='''DEFINE_REX_FUNC(sub_8228DD18) {
  // Block: 0x8228E388
  // stb r9,138(r11)
  REX_STORE_U8(r11.u32 + 138, r9.u8);
  // lwz r4,164(r31)
  ctx.r4.u64 = REX_LOAD_U32(r31.u32 + 164);
  // stb r8,241(r31)
  REX_STORE_U8(r31.u32 + 241, ctx.r8.u8);
  // mr r3,r4
  // Block: 0x8228E39C
  // lwz r11,164(r31)
  // Block: 0x8228E3B8
  // mr r3,r31
  // bl 0x82155750
  sub_82155750(ctx, base);
  // b 0x8228e590
  goto loc_8228E590;
}
'''
class WaterExit(unittest.TestCase):
    def test_exact_transition(self):
        result=patch(SOURCE)
        self.assertIn('REX_STORE_U8(REX_LOAD_U32(r31.u32 + 164) + 1005u, 0u);',result)
        self.assertLess(result.index('sub_82155750(ctx, base);'),result.index('HULK_TU1_WATER_EXIT_V1'))
        self.assertLess(result.index('HULK_TU1_WATER_EXIT_V1'),result.index('goto loc_8228E590;'))
        self.assertEqual(patch(result),result)
    def test_macros(self):
        self.assertIn('PPC_STORE_U8(PPC_LOAD_U32(ctx.r31.u32 + 0xa4) + 1005u, 0u);',patch(SOURCE.replace('REX_LOAD_','PPC_LOAD_').replace('REX_STORE_','PPC_STORE_').replace('r31.u32 + 164','ctx.r31.u32 + 0xa4')))
    def test_reject(self):
        for s in (SOURCE+SOURCE,SOURCE.replace('82155750(ctx, base)','82155754(ctx, base)'),SOURCE.replace('r31.u32 + 164','r30.u32 + 164'),SOURCE.replace('0x8228e590','0x8228e594')):
            with self.assertRaises(ValueError):patch(s)

if __name__=='__main__':unittest.main()
