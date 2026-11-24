#!/usr/bin/env python3
import unittest
from patch_sdk_hulk_world_stream_trace import INCLUDES, OLD_READ, apply_patch
from patch_sdk_hulk_city_distance import patch, READ_NEW, INCLUDE_NEW, CVAR_NEW

class TestCityDistance(unittest.TestCase):
    def test_original_read_path(self):
        original=INCLUDES+"\n"+OLD_READ
        trace, changed=apply_patch(original)
        self.assertTrue(changed)
        result, changed=patch(trace)
        self.assertTrue(changed)
        self.assertIn(READ_NEW,result)
        self.assertIn(INCLUDE_NEW,result)
        self.assertIn(CVAR_NEW,result)
        self.assertEqual(patch(result),(result,False))
        self.assertIn('buffer.first(*out_bytes_read)',result)
        self.assertIn('name=="QUICKDAT.ARC"',result)
        self.assertIn('file_handle_->Read(hulk::city::kLevelsOffset',result)
    def test_reject_unpatched_or_partial(self):
        with self.assertRaises(ValueError):
            patch(INCLUDES+"\n"+OLD_READ)
        with self.assertRaises(ValueError):
            patch(INCLUDE_NEW)

if __name__=="__main__":
    unittest.main()
