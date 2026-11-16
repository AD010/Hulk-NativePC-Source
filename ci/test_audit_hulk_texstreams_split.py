#!/usr/bin/env python3
"""Asset-free tests for USA-only original multipart TexStreams auditor."""
import pathlib,struct,sys,tempfile,unittest
sys.path.insert(0,str(pathlib.Path(__file__).parent))
import audit_hulk_texstreams_split as a
class AuditTests(unittest.TestCase):
    def test_wrong_region_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/"index.ind";p.write_bytes(b"old PAL index")
            with self.assertRaisesRegex(ValueError,"Wrong region"):a.usa_index(p)
    def test_name_reference_and_corruption(self):
        name="building_brick_ds";th=a.original_hash(name);sh=a.original_hash(name+"_STREAM")
        data=bytearray(b"LFXT"+name.encode()+b"\0"+bytes(100))
        end=4+len(name)
        struct.pack_into(">I",data,end+1+34,sh)
        key=f"{sh:08X}"
        self.assertEqual(a.original_link(data,th,{key:{}}),(key,34))
        self.assertIsNone(a.original_link(data,th,{}))
        with self.assertRaisesRegex(ValueError,"hash mismatch"):a.original_link(data,th^1,{key:{}})
        struct.pack_into(">I",data,end+1+22,sh)
        with self.assertRaisesRegex(ValueError,"Ambiguous"):a.original_link(data,th,{key:{}})
    def test_bounds_and_crc(self):
        self.assertEqual(a.original_hash("LFXTui_button"),a.original_hash("ui_button"))
        row={"offset":4,"size":8,"unknown":0}
        self.assertEqual(a.check_span(row,12),(4,8))
        for patch in ({"size":9},{"unknown":1},{"offset":2},{"size":0}):
            with self.assertRaises(ValueError):a.check_span(row|patch,12)
    def test_split_presence(self):
        with tempfile.TemporaryDirectory() as d:
            a1=pathlib.Path(d)/"TEXSTREA.7z.001";a2=pathlib.Path(d)/"TEXSTREA.7z.002"
            a1.write_bytes(b"part1")
            with self.assertRaises(FileNotFoundError):a.parts(a1)
            a2.write_bytes(b"part2")
            self.assertEqual(a.parts(a1),(a1,a2))
if __name__=="__main__":unittest.main()
