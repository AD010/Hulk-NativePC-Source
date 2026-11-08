#!/usr/bin/env python3
import unittest
from patch_sdk_hulk_world_stream_trace import (
    INCLUDES, OLD_READ, NEW_INCLUDES, NEW_READ, apply_patch)

class TestHulkWorldStreamTrace(unittest.TestCase):
    def test_idempotent_and_retains_result(self):
        baseline = "// fixture\n" + INCLUDES + "\n" + OLD_READ + "\n"
        result, changed = apply_patch(baseline)
        self.assertTrue(changed)
        self.assertIn("file_handle_->Read(byte_offset, buffer.data(), buffer.size(), out_bytes_read)", result)
        self.assertIn("return ok ? X_STATUS_SUCCESS : X_STATUS_END_OF_FILE;", result)
        self.assertIn("[HULK/STREAM]", result)
        self.assertIn("2000", result)
        self.assertEqual(apply_patch(result), (result, False))
    def test_mismatched_upstream_refused(self):
        with self.assertRaises(ValueError):
            apply_patch(INCLUDES + "\nnot the expected SDK function")
        with self.assertRaises(ValueError):
            apply_patch(NEW_INCLUDES + "\n" + OLD_READ)

if __name__ == "__main__":
    unittest.main()
