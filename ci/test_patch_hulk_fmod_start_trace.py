import unittest
from patch_hulk_fmod_start_trace import patch

SELECT = '''DEFINE_REX_FUNC(sub_82838650) {
  REX_FUNC_PROLOGUE();
loc_82838664:
  // lwz r11,76(r27)
  r11.u64 = PPC_LOAD_U32(r27.u32 + 76);
loc_828387A0:
  // lwz r11,0(r22)
  r11.u64 = PPC_LOAD_U32(r22.u32 + 0);
  // cmpwi cr6,r11,-1
loc_828387BC:
  // mulli r10,r11,24
loc_828388FC:
  // stw r11,48(r21)
  PPC_STORE_U32(r21.u32 + 48, r11.u32);
}
'''
START = '''DEFINE_REX_FUNC(sub_82839368) {
  REX_FUNC_PROLOGUE();
loc_828393DC:
  // bl 0x82838650
  sub_82838650(ctx, base);
loc_82839600:
  // lwz r10,20(r31)
  r10.u64 = PPC_LOAD_U32(r31.u32 + 20);
  // lwz r11,16(r31)
  r11.u64 = PPC_LOAD_U32(r31.u32 + 16);
  // ori r10,r10,8
  r10.u64 |= 8;
  // stw r10,20(r31)
  PPC_STORE_U32(r31.u32 + 20, r10.u32);
loc_82839B94:
  // li r3,0
  ctx.r3.s64 = 0;
}
'''

class TracePatch(unittest.TestCase):
    def test_install_and_idempotence(self):
        original = {'a':SELECT,'b':START}
        out = patch(original)
        self.assertIn('TraceFmodSelectionBegin(base, hulk_fmod_trace_child)', out['a'])
        self.assertIn('static_cast<uint32_t>(r21.u32), static_cast<uint32_t>(r11.u32)', out['a'])
        self.assertIn('TraceFmodChannelStart(base, hulk_fmod_trace_started_child)', out['b'])
        self.assertLess(out['a'].index('PPC_STORE_U32'),out['a'].index('TraceFmodSelectionStore'))
        self.assertLess(out['b'].index('PPC_STORE_U32'),out['b'].index('TraceFmodChannelStart'))
        self.assertLess(out['b'].index('ctx.r3.s64 = 0'),out['b'].index('TraceFmodChannelStart'))
        self.assertEqual(patch(out),out)
        self.assertIn('TraceFmodChannelStart',patch({'both':SELECT+START})['both'])
    def test_codegen_basic_blocks(self):
        # Fallthrough blocks have no loc_ label in real generated code.
        import re
        blocks = lambda source: re.sub(r'(?m)^loc_([0-9A-Fa-f]{8}):$', r'  // Block: 0x\1', source)
        a = blocks(SELECT).replace('  // lwz r11,76(r27)', '  // TODO: translation annotation\n  // lwz r11,76(r27)')
        b = blocks(START).replace('  // ori r10,r10,8', '  // Block: 0x82839608\n  // ori r10,r10,8')
        out = patch({'a':a,'b':b})
        self.assertIn('TraceFmodSelectionStore',out['a'])
        self.assertIn('TraceFmodChannelStart',out['b'])
        self.assertEqual(patch(out),out)
        with self.assertRaises(ValueError):
            patch({'a':a,'b':b.replace('  // Block: 0x82839608','  // Block: 0x82839600')})
        # A translated operand in a different basic block is not evidence.
        with self.assertRaises(ValueError):
            patch({'a':a,'b':b.replace('  PPC_STORE_U32','  // Block: 0x82839610\n  PPC_STORE_U32')})
    def test_rex_memory_macros_and_cached_record(self):
        a=SELECT.replace('PPC_', 'REX_')
        b=START.replace('PPC_', 'REX_')
        out=patch({'a':a,'b':b})
        self.assertIn('TraceFmodCachedRecord(base, hulk_fmod_trace_child, static_cast<uint32_t>(r11.u64))',out['a'])
        self.assertEqual(patch(out),out)
        # Upgrade a prior complete V2 patch without reinstalling its hooks.
        old={k:'\n'.join(l for l in v.splitlines() if 'HULK_FMOD_CACHED_RECORD_V1' not in l)+'\n' for k,v in out.items()}
        self.assertEqual(patch(old),out)
        with self.assertRaises(ValueError):patch({'a':a.replace('mulli r10,r11,24','mulli r10,r11,20'),'b':b})
    def test_reject(self):
        for a,b in [(SELECT,START.replace('ori r10,r10,8','ori r10,r10,4')),
                    (SELECT,START.replace('bl 0x82838650','bl 0x82838654')),
                    (SELECT.replace('r21.u32 + 48','r7.u32 + 48'),START),
                    (SELECT,START.replace('li r3,0','li r3,1')),
                    (SELECT+SELECT,START),(SELECT,START.replace('PPC_STORE_U32','other'))]:
            with self.subTest(a=a,b=b):
                with self.assertRaises(ValueError):patch({'a':a,'b':b})
        with self.assertRaises(ValueError):patch({'a':patch({'a':SELECT,'b':START})['a']})

if __name__ == '__main__':unittest.main()
