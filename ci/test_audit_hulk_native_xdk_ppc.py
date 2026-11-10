#!/usr/bin/env python3
import hashlib
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from audit_hulk_native_xdk_ppc import (
    audit, evidence_for, index_generated, load_candidates,
    rank_rectlist_clear_candidates
)

class EvidenceTests(unittest.TestCase):
    def test_labels_callers_and_literals_without_assigning_a_hook(self):
        with TemporaryDirectory() as d:
            base = Path(d)
            gen = base / 'generated'; gen.mkdir()
            (gen/'hulk_recomp.0.cpp').write_text('''DEFINE_REX_FUNC(sub_8289D888)
\t// lis r6,-16383
\t// ori r6,r6,8705
loc_8289D900:
\t// lwz r11,48(r3)
\t// stwu r6,4(r4)
DEFINE_REX_FUNC(sub_82110000)
\t// bl 0x8289D888
''')
            csv = base/'pm4_candidates.csv'
            csv.write_text('guest_address\n0x8289D888\n')
            xex = base/'default.xex'; xex.write_bytes(b'XEX2-test')
            report = audit(gen,csv,xex,hashlib.sha256(xex.read_bytes()).hexdigest())
            self.assertEqual(report['candidate_definitions_found'],1)
            c = report['candidates']['0x8289D888']
            self.assertEqual(c['validated_d3d_method'],None)
            self.assertEqual(c['pm4_literal_candidates'][0]['opcode'],'DRAW_INDX')
            self.assertEqual(c['r3_direct_memory_accesses'][0]['at'],'0x8289D900')
            self.assertEqual(c['direct_callers'][0]['caller_guest'],'0x82110000')
            with self.assertRaisesRegex(ValueError,'SHA mismatch'):
                audit(gen,csv,xex,'0'*64)

    def test_unverified_setter_neighborhood_is_exploratory_only(self):
        with TemporaryDirectory() as d:
            p = Path(d); gen = p/'gen'; gen.mkdir()
            (gen/'hulk_recomp.0.cpp').write_text('''DEFINE_REX_FUNC(sub_8289D888)
\t// bl 0x82880000
DEFINE_REX_FUNC(sub_82880000)
\t// stw r5,1024(r3)
''')
            csv = p/'c.csv'; csv.write_text('guest_address\n0x8289D888\n')
            x = p/'default.xex'; x.write_bytes(b'test')
            r = audit(gen,csv,x,hashlib.sha256(x.read_bytes()).hexdigest(),
                      0x82870000,0x828B0000)
            self.assertIn('0x82880000',r['regional_functions_with_r3_or_pm4_evidence'])
            self.assertIsNone(r['regional_functions_with_r3_or_pm4_evidence']['0x82880000']['validated_d3d_method'])
            self.assertEqual(r['candidate_definitions_found'],1)
            self.assertEqual(list(r['candidates']),['0x8289D888'])

    def test_only_actual_shader_then_draw_pattern_is_ranked(self):
        # Xenos PM4 headers: 0xC0002B00 = IM_LOAD_IMMEDIATE,
        # 0xC0003600 = DRAW_INDX_2. Decimal PPC immediates in ReXGlue.
        clear = [
            (0x82870000, 'lis r6,-16380'),  # PM4 type3 count=5
            (0x82870004, 'ori r6,r6,11008'),
            (0x82870008, 'lis r6,-16380'),
            (0x8287000C, 'ori r6,r6,11008'),
            (0x82870010, 'lis r6,-16384'),  # DRAW_INDX_2 count=1
            (0x82870014, 'ori r6,r6,13824'),
        ]
        only_shader = clear[:4]
        only_draw = clear[-2:]
        backward = [
            (0x82870300, 'lis r6,-16384'),
            (0x82870304, 'ori r6,r6,13824'),
            (0x82870310, 'lis r6,-16380'),
            (0x82870314, 'ori r6,r6,11008'),
        ]
        bodies = {
            0x82870000: clear, 0x82870100: only_shader,
            0x82870200: only_draw, 0x82870300: backward,
        }
        ranked = rank_rectlist_clear_candidates(bodies, {})
        self.assertEqual(len(ranked), 1)
        self.assertEqual(ranked[0]['guest_function'], '0x82870000')
        self.assertEqual(ranked[0]['draw_indx2_after_im_load'], 1)
        self.assertEqual(ranked[0]['immediate_shader_loads'], 2)
        # Zero-count shader headers are not enough to identify ClearF.
        malformed = [(0x82870000, 'lis r6,-16384'),
                     (0x82870004, 'ori r6,r6,11008')] + clear[-2:]
        self.assertFalse(rank_rectlist_clear_candidates({0x82870400: malformed}, {}))
        self.assertEqual(ranked[0]['confidence'], 'pattern_only_not_hook_abi')
        self.assertIn('other_XDK_helper', ranked[0]['candidate_roles'][0])
        with self.assertRaises(ValueError):
            rank_rectlist_clear_candidates(bodies, {}, limit=0)

    def test_audit_never_emits_unverified_clearf_hook(self):
        with TemporaryDirectory() as d:
            root = Path(d)
            gen = root/'gen'; gen.mkdir()
            (gen/'hulk_recomp.0.cpp').write_text(
                'DEFINE_REX_FUNC(sub_82870000)\n'
                '  // lis r6,-16380\n'
                '  // ori r6,r6,11008\n'
                '  // lis r6,-16384\n'
                '  // ori r6,r6,13824\n')
            csv = root/'c.csv'
            csv.write_text('guest_address\n0x82870000\n')
            xex = root/'default.xex'; xex.write_bytes(b'test xex')
            report = audit(gen, csv, xex, hashlib.sha256(xex.read_bytes()).hexdigest())
            hits = report['unverified_xdk_rectlist_clear_candidates']
            self.assertEqual(len(hits), 1)
            self.assertEqual(hits[0]['guest_function'], '0x82870000')
            self.assertNotIn('ClearF', report.get('validated_hooks', {}))

    def test_duplicates_and_missing_are_visible(self):
        with TemporaryDirectory() as d:
            base=Path(d); gen=base/'gen'; gen.mkdir()
            (gen/'hulk_recomp.0.cpp').write_text('''DEFINE_REX_FUNC(sub_8289D888)
\t// bl 0x82110000
''')
            csv=base/'c.csv'; csv.write_text('guest_address\n0x8289D888\n0x8288D458\n')
            xex=base/'a'; xex.write_bytes(b'example')
            report=audit(gen,csv,xex,hashlib.sha256(xex.read_bytes()).hexdigest())
            self.assertEqual(report['missing_guest_definitions'],['0x8288D458'])
            csv.write_text('guest_address\n0x8289D888\n0x8289D888\n')
            with self.assertRaisesRegex(ValueError,'duplicate'):
                load_candidates(csv)

if __name__=='__main__': unittest.main()
