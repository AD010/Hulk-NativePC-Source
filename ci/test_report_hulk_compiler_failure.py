#!/usr/bin/env python3
"""No Hulk game data needed for diagnostic redaction regression tests."""
import unittest
from report_hulk_compiler_failure import summarize, stage_report, hint_report

class RedactionTests(unittest.TestCase):
    def test_compiler_codes_without_private_source(self):
        raw = (
            r'D:\a\path\native_x_renderer_git\src\renderer.cpp(120): '
            r'error C2664: sub_821ABCDE cannot convert default.xex' + '\n'
            r'D:\a\path\generated\hulk_recomp.13.cpp(29): error C2143: syntax' + '\n'
            r'D:\a\path\native_x_renderer_git\src\renderer.cpp(122): error C2664: other' + '\n'
        )
        out = '\n'.join(summarize(raw))
        self.assertIn('C2664 [renderer.cpp] count=2', out)
        self.assertIn('C2143 [source-unavailable]', out)
        self.assertNotIn('821ABCDE', out)
        self.assertNotIn('default.xex', out)
        self.assertNotIn('hulk_recomp.13.cpp', out)

    def test_no_raw_error_lines_or_credentials(self):
        raw = 'ModuleNotFoundError: No module named secret_thing\n'
        out = '\n'.join(summarize(raw))
        self.assertNotIn('secret_thing', out)
        self.assertIn('modulenotfounderror', out.lower())
        self.assertIn('No classified', '\n'.join(summarize('Build complete')))

    def test_stage_and_error_hints_only_whitelisted(self):
        raw = (
            'RUN: python D:\\\\code\\\\ci\\\\patch_sdk_hulk_index_cache.py D:\\\\secrets\\\\default.xex\n'
            'RUN: cmake --build out/build/win-amd64 --config Release --parallel\n'
            'RUN: rexglue.exe codegen D:\\\\private\\\\game\\\\default.xex\n'
            'error: unrecognized arguments: --secret=PASSWORD\n'
        )
        stages = ' '.join(stage_report(raw))
        hints = ' '.join(hint_report(raw))
        self.assertIn('python script=patch_sdk_hulk_index_cache.py', stages)
        self.assertIn('cmake', stages)
        self.assertIn('rexglue.exe', stages)
        self.assertNotIn('default.xex', stages)
        self.assertNotIn('PASSWORD', stages + hints)
        self.assertIn('invalid-cli-arguments', hints)

    def test_error_group_count_limit(self):
        raw = '\n'.join(f'file{i}.cpp: error C{i+1000}: private token' for i in range(50))
        out = summarize(raw, cap=3)
        self.assertEqual(len(out), 4)
        self.assertIn('classified_error_lines=50', out[-1])

if __name__ == '__main__':
    unittest.main()
