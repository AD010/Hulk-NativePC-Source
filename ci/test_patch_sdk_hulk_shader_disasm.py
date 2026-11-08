#!/usr/bin/env python3
"""Synthetic source-level regression for Xenos vertex disassembly null name."""
import unittest
from patch_sdk_hulk_shader_disasm import OLD, NEW, patch_text


class TestShaderDisassembly(unittest.TestCase):
    def test_guard_and_unknown_numeric_identifier(self):
        content, changed = patch_text("prefix\n" + OLD + "\nsuffix")
        self.assertTrue(changed)
        self.assertIn("if (format_name)", content)
        self.assertIn("format_id <", content)
        self.assertIn("unlisted(0x{:X})", content)
        self.assertNotIn(OLD, content)
        self.assertEqual(patch_text(content), (content, False))

    def test_refuse_unknown_or_partial_source(self):
        with self.assertRaises(ValueError):
            patch_text("shader code moved")
        with self.assertRaises(ValueError):
            patch_text(NEW + "\n" + OLD)


if __name__ == "__main__":
    unittest.main()
