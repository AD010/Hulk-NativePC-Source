#!/usr/bin/env python3
"""Source-only Hulk save-root regression: no private saves/code required."""
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from patch_sdk_hulk_native_save_root import (
    INCLUDE_ANCHOR, NS_ANCHOR, ROOT_OLD, HEADER_OLD,
    ROOT_NEW, HEADER_NEW, patch_cpp, SOURCE
)

SDK = pathlib.Path(sys.argv.pop(1)) if len(sys.argv)>1 and not sys.argv[1].startswith("-") else None

class TestHulkNativeSaveRoot(unittest.TestCase):
    def test_atomic_patch_against_real_sdk_or_fixture(self):
        cpp=(SDK/SOURCE).read_text(encoding="utf-8") if SDK else (
            INCLUDE_ANCHOR+NS_ANCHOR+ROOT_OLD+"\n"+HEADER_OLD)
        installed,changed=patch_cpp(cpp)
        self.assertTrue(changed)
        self.assertEqual(patch_cpp(installed),(installed,False))
        self.assertIn('REXCVAR_DEFINE_BOOL(hulk_native_pc_saves, false',installed)
        self.assertIn("HULK/NATIVE-PC-SAVE-ROOT",installed)
        self.assertIn("HULK/NATIVE-PC-SAVE-HEADER",installed)
        self.assertIn("return native / \"Headers\" /",installed)
        self.assertLess(installed.index(ROOT_NEW),installed.index(HEADER_NEW))
        with self.assertRaises(ValueError):
            patch_cpp(INCLUDE_ANCHOR+NS_ANCHOR+ROOT_NEW+HEADER_OLD)
        with self.assertRaises(ValueError):
            patch_cpp("unknown upstream content manager")

    def test_actual_cpp_path_and_title_gate(self):
        compiler=shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            self.skipTest("No C++ compiler")
        root=pathlib.Path(__file__).resolve().parents[1]
        header=root/"native_port/src/native_windows_save_root.h"
        code=r'''
#include <cassert>
#include <filesystem>
#include <string>
#include "native_windows_save_root.h"
int main() {
  using namespace hulk::native_save;
  const std::filesystem::path documents = std::filesystem::path("C:/Users/Test/OneDrive/Documents");
  const auto saved=RootUnderDocuments(documents,0x534507F1,1);
  assert(saved ==
    documents/"SEGA"/"The Incredible Hulk"/"NativePC"/"SavedGames");
  assert(saved.filename() == "SavedGames");
  assert(saved.string().find("B13EBABEBABEBABE")==std::string::npos);
  assert(RootUnderDocuments(documents,0x534507F2,1).empty());
  assert(RootUnderDocuments(documents,0x534507F1,2).empty());
  assert(RootUnderDocuments({},0x534507F1,1).empty());
#ifndef _WIN32
  assert(WindowsRoot(0x534507F1,1,true).empty());
#endif
  assert(WindowsRoot(0x534507F1,1,false).empty());
}
'''
        with tempfile.TemporaryDirectory() as temp:
            p=pathlib.Path(temp)
            source=p/"main.cpp";source.write_text(code,encoding="utf-8")
            out=p/"native_save_path_test"
            subprocess.run([compiler,"-std=c++17","-I",str(header.parent),
                            str(source),"-o",str(out)],check=True)
            subprocess.run([str(out)],check=True)

if __name__=="__main__":
    unittest.main()
