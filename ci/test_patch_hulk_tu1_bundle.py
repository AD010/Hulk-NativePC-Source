import unittest
from patch_hulk_tu1_bundle import plan,FIXES
from test_patch_hulk_tu1_water_origin import SOURCE as ORIGIN
from test_patch_hulk_tu1_water_trajectory import SOURCE as TRAJECTORY
from test_patch_hulk_tu1_water_exit import SOURCE as EXIT
from test_patch_hulk_tu1_ledge_region import SOURCE as LEDGE

def fixtures():
    return {'a':ORIGIN,'b':TRAJECTORY.rsplit('}',1)[0]+EXIT.split('{',1)[1], 'c':LEDGE}

class Bundle(unittest.TestCase):
    def test_complete(self):
        original=fixtures();updated=plan(original)
        self.assertEqual(plan(updated),updated)
        for fix in FIXES:self.assertIn(fix.MARKER,'\n'.join(updated.values()))
        self.assertEqual(original,fixtures())
    def test_failure_keeps_input(self):
        original=fixtures();original['c']=original['c'].replace('f28.f64 =','f27.f64 =');before=dict(original)
        with self.assertRaises(ValueError):plan(original)
        self.assertEqual(original,before)
    def test_combined_translation_unit(self):
        original=fixtures();combined={'all':'\n'.join(original.values())}
        updated=plan(combined);self.assertEqual(plan(updated),updated)

if __name__=='__main__':unittest.main()
