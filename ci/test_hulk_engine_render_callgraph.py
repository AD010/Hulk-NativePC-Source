#!/usr/bin/env python3
"""Source-only regression tests for high-level PPC render-call discovery."""
import hashlib
import tempfile
import unittest
from pathlib import Path
from hulk_engine_render_callgraph import (
    read_hooks, scan_direct_calls, analyze, verify_us_source, USA_XEX_SHA256
)
from test_audit_hulk_xex_xdk import make_xex

HOOKS = """[hooks]
0x8288DF30=DrawVertices
0x8288E318=DrawIndexedVertices
0x828897F8=Resolve
0x8287F2C0=SetRenderTarget
0x8287F628=SetDepthStencilSurface
0x8288A900=BeginTiling
0x8288AD98=EndTiling
0x82880778=Present
"""

class RenderGraph(unittest.TestCase):
    def test_high_level_engine_ancestor_reaches_draw_resolve_present(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cfg = root/'profile.ini'
            cfg.write_text(HOOKS, encoding='utf-8')
            src = root/'generated'
            src.mkdir()
            (src/'incredible_hulk_recomp.0.cpp').write_text(
                'DEFINE_REX_FUNC(sub_82400000)\n'
                '  // bl 0x8288DF30\n'
                '  // bl 0x828897F8\n'
                'DEFINE_REX_FUNC(sub_82400100)\n'
                '  // bl 0x82880778\n'
                'DEFINE_REX_FUNC(sub_82500000)\n'
                '  // bl 0x82400000\n'
                '  // bl 0x82400100\n'
                '  // b 0x8288DF30\n',
                encoding='utf-8')
            hooks = read_hooks(cfg)
            reverse, files, functions, found = scan_direct_calls(src, set(hooks))
            self.assertEqual((files, functions), (1, 3))
            self.assertFalse(found)
            result = analyze(reverse, hooks, depth=2, found_hooks=found)
            self.assertFalse(result['hooks']['Present']['present_in_usa_codegen'])
            self.assertIn('0x82880778', result['missing_xdk_function_bodies'])
            top = next(x for x in result['candidate_ancestors']
                       if x['guest_function'] == '0x82500000')
            self.assertEqual(set(top['hook_groups']), {'draw', 'resolve', 'present'})
            self.assertEqual(top['evidence']['DrawVertices']['distance'], 2)
            self.assertEqual(top['evidence']['Present']['distance'], 2)

    def test_source_usa_sha_and_xdk_provenance(self):
        with tempfile.TemporaryDirectory() as tmp:
            file = Path(tmp) / 'default.xex'
            data = make_xex()
            file.write_bytes(data)
            known = hashlib.sha256(data).hexdigest()
            report = verify_us_source(file, known)
            self.assertEqual(report['source_xex_sha256'], known)
            self.assertEqual(report['source_title_id'], '534507F1')
            self.assertTrue(report['source_xdk_6534_verified'])
            self.assertEqual(len(USA_XEX_SHA256), 64)
            with self.assertRaisesRegex(ValueError, 'USA XEX SHA-256 mismatch'):
                verify_us_source(file)

    def test_exact_hook_bodies_and_duplicate_rejection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            f = root/'incredible_hulk_recomp.0.cpp'
            f.write_text(
                'DEFINE_REX_FUNC(sub_8288DF30)\n'
                '  // bl 0x8288E318\n'
                'DEFINE_REX_FUNC(sub_8288E318)\n', encoding='utf-8')
            reverse, _, count, found = scan_direct_calls(
                root, {0x8288DF30, 0x8288E318})
            self.assertEqual(count, 2)
            self.assertEqual(found, {0x8288DF30, 0x8288E318})
            self.assertEqual(reverse[0x8288E318][0][0], 0x8288DF30)
            with f.open('a', encoding='utf-8') as out:
                out.write('DEFINE_REX_FUNC(sub_8288DF30)\n')
            with self.assertRaisesRegex(ValueError, 'Duplicate USA XDK'):
                scan_direct_calls(root, {0x8288DF30})

    def test_missing_files_and_unverified_addresses(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaises(ValueError):
                scan_direct_calls(root)
            ini = root/'profile.ini'
            ini.write_text(HOOKS.replace('0x82880778=Present',
                                         '0x12345678=Present'), encoding='utf-8')
            with self.assertRaises(ValueError):
                read_hooks(ini)

if __name__ == '__main__':
    unittest.main()
