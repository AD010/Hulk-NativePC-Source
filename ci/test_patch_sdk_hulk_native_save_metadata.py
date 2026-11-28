import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from patch_sdk_hulk_native_save_metadata import patch_cpp, SOURCE, PAIRS

SDK=pathlib.Path(sys.argv.pop(1)) if len(sys.argv)>1 and not sys.argv[1].startswith('-') else None
class NativeMetadata(unittest.TestCase):
    def test_transform(self):
        source=(SDK/SOURCE).read_text(encoding='utf-8') if SDK else '\n'.join(
            ['// HULK/NATIVE-PC-SAVE-ROOT','// HULK/SAVE-COMMIT']+[a for a,b in PAIRS])
        result,changed=patch_cpp(source)
        self.assertTrue(changed)
        self.assertEqual(patch_cpp(result),(result,False))
        for old,new in PAIRS:self.assertIn(new,result)
        for old,new in PAIRS:
            with self.assertRaises(ValueError):patch_cpp(source.replace(old,'missing'))
        with self.assertRaises(ValueError):patch_cpp(result.replace(PAIRS[0][1],PAIRS[0][0]))
    def test_native_codec(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        if not compiler:self.skipTest('No portable C++ compiler')
        root=pathlib.Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp:
            exe=pathlib.Path(temp)/'metadata_test'
            subprocess.run([compiler,'-std=c++17','-I',str(root/'native_port/src'),
                            str(root/'ci/native_save_metadata_test.cpp'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
if __name__=='__main__':unittest.main()
