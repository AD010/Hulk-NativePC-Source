import unittest
from patch_hulk_tu1_water_trajectory import patch

SOURCE='''DEFINE_REX_FUNC(sub_8228DD18) {
  // Block: 0x8228E254
  // lfs f0,84(r4)
  temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 84);
  f0.f64 = double(temp.f32);
  // fsubs f12,f11,f0
  f12.f64 = double(float(f11.f64 - f0.f64));
  // Block: 0x8228E264
  // fmuls f0,f12,f0
  f0.f64 = double(float(f12.f64 * f0.f64));
  // lfs f12,4648(r11)
  // Block: 0x8228E2E0
  // lfs f13,192(r31)
  // lfs f10,220(r31)
  // fmuls f12,f12,f0
  // fnmsubs f0,f12,f0,f31
  // fmuls f0,f0,f13
  f0.f64 = double(float(f0.f64 * f13.f64));
  // stfs f0,196(r31)
  temp.f32 = float(f0.f64);
  REX_STORE_U32(r31.u32 + 196, temp.u32);
  // fsubs f13,f11,f0
  f13.f64 = double(float(f11.f64 - f0.f64));
  // stfs f13,200(r31)
  temp.f32 = float(f13.f64);
  REX_STORE_U32(r31.u32 + 200, temp.u32);
  // lwz r11,0(r4)
  // fadds f29,f10,f0
  f29.f64 = double(float(f10.f64 + f0.f64));
  // lwz r11,264(r11)
}
'''
class TrajectoryFix(unittest.TestCase):
    def test_exact_translation(self):
        result=patch(SOURCE)
        self.assertIn('f0.f64 = double(float(f12.f64 * 2.0));',result)
        self.assertIn('std::fma(f0.f64, f13.f64, f10.f64)',result)
        self.assertIn('temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 84); f11.f64',result)
        self.assertIn('f29.f64 = f0.f64;',result)
        self.assertEqual(result.count('HULK_TU1_WATER_TRAJECTORY_V1'),4)
        self.assertEqual(patch(result),result)
    def test_reject(self):
        for s in (SOURCE+SOURCE,SOURCE.replace('f11.f64 - f0.f64','f12.f64 - f0.f64'),SOURCE.replace('+ 200,','+ 204,'),SOURCE.replace('f29.f64 = double','f28.f64 = double'),SOURCE.replace('0x8228E264','0x8228E268')):
            with self.assertRaises(ValueError):patch(s)
    def test_local_and_context_registers(self):
        result=patch(SOURCE.replace('f0.f64','ctx.f0.f64').replace('f11.f64','ctx.f11.f64'))
        self.assertIn('ctx.f11.f64 = double(std::bit_cast<float>',result)

if __name__=='__main__':unittest.main()
