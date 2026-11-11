#!/usr/bin/env python3
"""Run actual production leaf validator and bounded getter, not a Python model."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from patch_sdk_hulk_save_names import HEADER, SOURCE, OLD_NAME, NEW_NAME, HELPER, GUARDS, patch
SDK=Path(sys.argv.pop(1)) if len(sys.argv)>1 and not sys.argv[1].startswith('-') else None

class SaveNames(unittest.TestCase):
    def test_transform(self):
        h=(SDK/HEADER).read_text() if SDK else OLD_NAME
        s=(SDK/SOURCE).read_text() if SDK else 'static const char* kThumbnailFileName;\n'+''.join(
            f'void ContentManager::{m}(int data) {{\n}}\n' for m in GUARDS)
        h,s,_=patch(h,s)
        self.assertEqual(patch(h,s),(h,s,False))
        with self.assertRaises(ValueError): patch(OLD_NAME, 'drift')
        with self.assertRaises(ValueError): patch(NEW_NAME, 'drift')
        self.assertNotIn('strlen(file_name_raw)',h)
    def test_atomic_metadata_transaction(self):
        from patch_sdk_hulk_save_names import COMMIT_OLD, COMMIT_NEW, patch_commit
        old=(SDK/SOURCE).read_text() if SDK else COMMIT_OLD
        fixed, _=patch_commit(old)
        self.assertEqual(patch_commit(fixed),(fixed,False))
        with self.assertRaises(ValueError): patch_commit('drift')
        self.assertNotIn('CreateEmptyFile(header_path)',fixed)
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source=Path(__file__).resolve().parents[1]/'native_port/tests/native_save_commit_test.cpp'
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/'commit.exe'
            subprocess.run([compiler,'-std=c++17',str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe),d],check=True)

    def test_actual_cpp(self):
        cpp=r'''#include <algorithm>
#include <string>
#include <string_view>
#include <cstdint>
#include <cassert>
#include <array>
template<class T, size_t N> constexpr size_t countof(const T(&)[N]){return N;}
'''+HELPER+r'''
struct ContentName {
  char file_name_raw[42];
  std::string name() const { std::string value;
'''+NEW_NAME+r'''
    return value;
  }
};
int main() {
  for (auto bad : {"", ".", "..", "../old", "..\\old", "C:save", "C:\\save",
      "\\\\server\\share", "slot/other", "file:stream", "save.", "save ",
      "save?", "save*", "CON", "con.sav", "PRN.txt", "LPT1", "COM9.data",
      "CON .txt", "CONIN$", "CONOUT$"}) assert(!HulkSafeContentLeaf(bad));
  assert(!HulkSafeContentLeaf(std::string("slot\0tail",9)));
  assert(!HulkSafeContentLeaf(std::string("COM\xc2\xb9")));
  assert(!HulkSafeContentLeaf(std::string(43,'a')));
  for (auto good : {"HULK", "HULK.SAV", "Slot 1", "settings.dat", "COM10", "CONsole"})
    assert(HulkSafeContentLeaf(good));
  // Heap allocation ends exactly at the field: no trailing NUL or readable padding.
  auto name = new ContentName;
  std::fill_n(name->file_name_raw,42,'a');
  assert(name->name()==std::string(42,'a'));
  for(unsigned i=0;i<42;++i){
    std::fill_n(name->file_name_raw,42,'a');name->file_name_raw[i]='\0';
    assert(name->name()==std::string(i,'a'));
  }
  delete name;
}
'''
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'names.cpp';exe=Path(d)/'names.exe';src.write_text(cpp)
            subprocess.run([compiler,'-std=c++17',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__=='__main__':unittest.main()
