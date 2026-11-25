import unittest
from audit_hulk_cpu_baseline import audit, BASELINE

class Baseline(unittest.TestCase):
    def test_safe(self):
        root = "C:/work/hulk"
        entries = [{"file": root+"/src/a.cpp", "command": "clang++ "+BASELINE},
                   {"file": root+"/generated/b.cpp", "arguments": ["clang++","-march=x86-64-v2","-msse4.1"]}]
        self.assertEqual(audit(entries,root),(2,[]))
    def test_reject(self):
        for flag in ["-mavx","-mavx2","-mavx512f","-mfma","-mf16c","/arch:AVX2","-march=native","-march=x86-64-v3"]:
            n, bad = audit([{"file":"/hulk/src/a.cpp","command":"clang++ "+flag}],"/hulk")
            self.assertEqual(n,1)
            self.assertEqual(len(bad),1,flag)
    def test_vendored(self):
        entries = [{"file":"/hulk/thirdparty/ffmpeg/a.c","command":"clang -mavx2"},
                   {"file":"/hulk/thirdparty/diligent-core/a.cpp","command":"clang++ -mavx2"}]
        self.assertEqual(audit(entries,"/hulk"),(1,["-mavx2"]))
    def test_empty(self):
        with self.assertRaises(ValueError):
            audit([],"/hulk")
if __name__=="__main__":
    unittest.main()
