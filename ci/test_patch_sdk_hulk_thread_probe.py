#!/usr/bin/env python3
import unittest
from patch_sdk_hulk_thread_probe import patch_text, INCLUDE, NAMESPACE, CREATE, AFFINITY

class TestCpuThreadProbe(unittest.TestCase):
    def test_source_patch_noninvasive_idempotent(self):
        source = '\n'.join([INCLUDE, NAMESPACE, CREATE, AFFINITY])
        result, changed = patch_text(source)
        self.assertTrue(changed)
        self.assertIn('REXCVAR_DEFINE_BOOL(hulk_guest_thread_probe, false', result)
        self.assertIn('if (XSUCCEEDED(result)', result)
        self.assertIn('thread->SetAffinity(affinity);', result)
        self.assertIn('REXKRNL_IMPORT_RESULT("ExCreateThread"', result)
        self.assertEqual(patch_text(result), (result, False))
        self.assertEqual(result.count('thread->SetAffinity(affinity);'), 1)
        self.assertEqual(result.count('REXCVAR_DEFINE_BOOL(hulk_guest_thread_probe'), 1)
    def test_wrong_sdk_fails_closed(self):
        for text in ('wrong version', INCLUDE + NAMESPACE + CREATE,
                     INCLUDE + NAMESPACE + CREATE + AFFINITY + 'HULK/CPU-THREAD-PROBE'):
            with self.assertRaises(ValueError):
                patch_text(text)

if __name__ == '__main__':
    unittest.main()
