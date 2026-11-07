#!/usr/bin/env python3
import hashlib
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from audit_hulk_native_xdk_ppc import audit, evidence_for, index_generated, load_candidates

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
