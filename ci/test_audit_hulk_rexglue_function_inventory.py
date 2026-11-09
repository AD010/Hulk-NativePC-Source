#!/usr/bin/env python3
import tempfile,unittest
from pathlib import Path
from audit_hulk_rexglue_function_inventory import entries,audit

class FunctionInventory(unittest.TestCase):
    def test_spans_and_helper(self):
        with tempfile.TemporaryDirectory() as d:
            a,b=Path(d)/"missing.toml",Path(d)/"indirect.toml"
            a.write_text("[functions]\n0x82110020 = {}\n",encoding="utf8")
            b.write_text("[functions]\n0x82110100 = {end = 0x82110110}\n"
                         "0x82110200 = {end = 0x82112000}\n"
                         "0x827377B0 = {end = 0x827377C0}\n",encoding="utf8")
            result=audit(entries(a),entries(b))
            self.assertEqual(result["missing"],1)
            self.assertEqual(result["indirect"],3)
            self.assertEqual(result["indirect_overlaps"],[])
            self.assertEqual(result["helper_region_starts"],["0x827377B0"])
            self.assertEqual(result["large_spans"][0]["start"],"0x82110200")
    def test_bad_inputs(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"bad.toml"
            for x in ("0x82110001 = {}","0x82110010 = {end = 0x82110010}","0x82000000 = {}"):
                p.write_text("[functions]\n"+x+"\n",encoding="utf8")
                with self.assertRaises(ValueError):entries(p)

if __name__=="__main__":unittest.main()
