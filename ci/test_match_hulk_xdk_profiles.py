#!/usr/bin/env python3
import unittest
from match_hulk_xdk_profiles import normalized, compare

class FingerprintTests(unittest.TestCase):
    def test_absolute_relocations_do_not_break_matching(self):
        self.assertEqual(normalized(0x48123401),normalized(0x48004501))
        self.assertEqual(normalized(0x40c2fffc),normalized(0x40c20010))
    def test_specific_sequence(self):
        a=[0x60000000+i for i in range(40)]
        b=a.copy();a[17]=0x48004501;b[17]=0x48123401
        found=compare({0x828998F8:tuple(a)},{
            0x82641618:tuple(b),
            0x82640F60:tuple(0x70000000+i for i in range(40))},[0x828998F8])
        self.assertEqual(found['0x828998F8'][0]['guest_address'],'0x82641618')
        self.assertGreater(found['0x828998F8'][0]['shared_threegrams'],30)
        self.assertFalse(found['0x828998F8'][0]['validated_d3d_abi'])
    def test_unknown_entry(self):
        self.assertEqual(compare({}, {}, [0x82123456]),{'0x82123456':[]})
if __name__=='__main__':unittest.main()
