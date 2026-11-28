import unittest
from patch_hulk_tu1_water_origin import patch

SOURCE = '''DEFINE_REX_FUNC(sub_8228D760) {
  // Block: 0x8228D7A0
  // stfs f11,216(r31)
  REX_STORE_U32(r31.u32 + 216, temp.u32);
  // lfs f11,84(r11)
  temp.u32 = REX_LOAD_U32(r11.u32 + 84);
  // stfs f11,220(r31)
  temp.f32 = float(f11.f64);
  REX_STORE_U32(r31.u32 + 220, temp.u32);
  // lfs f11,88(r11)
  temp.u32 = REX_LOAD_U32(r11.u32 + 88);
}
'''

class OriginFix(unittest.TestCase):
    def test_exact_store(self):
        result=patch(SOURCE)
        self.assertIn('std::bit_cast<float>(static_cast<uint32_t>(temp.u32)) > -1.0f ? 0xc0a00000u : temp.u32',result)
        self.assertEqual(patch(result),result)
        self.assertEqual(result.count('HULK_TU1_WATER_ORIGIN_V1'),1)
        self.assertIn('REX_STORE_U32(r31.u32 + 216, temp.u32);',result)
    def test_macro_variants(self):
        for macro in ('PPC_STORE_U32','REX_MM_STORE_U32'):
            self.assertIn('HULK_TU1_WATER_ORIGIN_V1',patch(SOURCE.replace('REX_STORE_U32',macro).replace('r31.u32 + 220','ctx.r31.u32 + 0xdc')))
    def test_reject(self):
        for source in (SOURCE+SOURCE,SOURCE.replace('220(r31)','224(r31)'),SOURCE.replace('+ 220,','+ 224,'),SOURCE.replace('0x8228D7A0','0x8228D7A4')):
            with self.assertRaises(ValueError):patch(source)

if __name__=='__main__':unittest.main()
