import unittest
from patch_hulk_fmod_start_trace import patch

SELECT = '''DEFINE_REX_FUNC(sub_82838650) {
  REX_FUNC_PROLOGUE();
loc_82838664:
  // lwz r9,76(r3)
  r9.u64 = PPC_LOAD_U32(ctx.r3.u32 + 76);
loc_828388FC:
  // stw r8,48(r9)
  PPC_STORE_U32(r9.u32 + 48, r8.u32);
}
'''
START = '''DEFINE_REX_FUNC(sub_82839400) {
  REX_FUNC_PROLOGUE();
loc_82839500:
  // bl 0x82838650
  sub_82838650(ctx, base);
loc_82839600:
  // lwz r10,20(r31)
  r10.u64 = PPC_LOAD_U32(r31.u32 + 20);
  // ori r10,r10,8
  r10.u64 |= 8;
  // stw r10,20(r31)
  PPC_STORE_U32(r31.u32 + 20, r10.u32);
}
'''

class TracePatch(unittest.TestCase):
    def test_install_and_idempotence(self):
        original = {'a':SELECT,'b':START}
        out = patch(original)
        self.assertIn('TraceFmodSelectionBegin(base, hulk_fmod_trace_child)', out['a'])
        self.assertIn('static_cast<uint32_t>(r9.u32), static_cast<uint32_t>(r8.u32)', out['a'])
        self.assertIn('TraceFmodChannelStart(base, static_cast<uint32_t>(r31.u32))', out['b'])
        self.assertLess(out['a'].index('PPC_STORE_U32'),out['a'].index('TraceFmodSelectionStore'))
        self.assertLess(out['b'].index('PPC_STORE_U32'),out['b'].index('TraceFmodChannelStart'))
        self.assertEqual(patch(out),out)
        self.assertIn('TraceFmodChannelStart',patch({'both':SELECT+START})['both'])
    def test_reject(self):
        for a,b in [(SELECT,START.replace('ori r10,r10,8','ori r10,r10,4')),
                    (SELECT,START.replace('bl 0x82838650','bl 0x82838654')),
                    (SELECT.replace('r9.u32 + 48','r7.u32 + 48'),START),
                    (SELECT+SELECT,START),(SELECT,START.replace('PPC_STORE_U32','other'))]:
            with self.subTest(a=a,b=b):
                with self.assertRaises(ValueError):patch({'a':a,'b':b})
        with self.assertRaises(ValueError):patch({'a':patch({'a':SELECT,'b':START})['a']})

if __name__ == '__main__':unittest.main()
