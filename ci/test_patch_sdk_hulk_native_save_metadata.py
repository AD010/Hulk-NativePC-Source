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
    def test_native_enumerator(self):
        from patch_sdk_hulk_native_save_metadata import (
            patch_xam, XAM_SOURCE, XAM_INCLUDE, XAM_ENUM, XAM_ENUM_NEW,
            XAM_CREATE, XAM_CREATE_NEW)
        source=(SDK/XAM_SOURCE).read_text(encoding='utf-8') if SDK else XAM_INCLUDE+XAM_ENUM+'\n'+XAM_CREATE
        result,changed=patch_xam(source)
        self.assertTrue(changed)
        self.assertIn(XAM_ENUM_NEW,result)
        self.assertIn(XAM_CREATE_NEW,result)
        self.assertIn('result = header_result;',result)
        self.assertIn('disposition = kDispositionState::Unknown;',result)
        self.assertIn('content_manager->CloseContent(root_name);',result)
        self.assertIn('xuid != 0 && !hulk_single_root',result)
        self.assertEqual(patch_xam(result),(result,False))
        for old in (XAM_INCLUDE,XAM_ENUM,XAM_CREATE):
            with self.assertRaises(ValueError):patch_xam(source.replace(old,'missing'))
        with self.assertRaises(ValueError):patch_xam(result.replace(XAM_ENUM_NEW,XAM_ENUM))
        with self.assertRaises(ValueError):patch_xam(result.replace(XAM_CREATE_NEW,XAM_CREATE))
    def test_backup_transform(self):
        from patch_sdk_hulk_native_save_metadata import (
            patch_backup, BACKUP_ANCHOR, BACKUP_HELPER, DELETE_ANCHOR, DELETE_GUARD)
        source=(SDK/SOURCE).read_text(encoding='utf-8') if SDK else '\n'.join(
            [BACKUP_ANCHOR,DELETE_ANCHOR,DELETE_ANCHOR])
        result,changed=patch_backup(source)
        self.assertTrue(changed)
        self.assertIn(BACKUP_HELPER,result)
        self.assertEqual(result.count(DELETE_GUARD+DELETE_ANCHOR),2)
        self.assertEqual(patch_backup(result),(result,False))
        with self.assertRaises(ValueError):patch_backup(source.replace(DELETE_ANCHOR,'missing',1))
        with self.assertRaises(ValueError):patch_backup(result.replace(DELETE_GUARD,'',1))
    def test_native_flush_transform(self):
        from patch_sdk_hulk_native_save_metadata import (
            patch_flush, FLUSH_HEADER, XAM_SOURCE, FLUSH_DECL, FLUSH_CPP,
            FLUSH_XAM, FLUSH_DECL_NEW, FLUSH_CPP_NEW, FLUSH_XAM_NEW,
            FLUSH_CLOSE, FLUSH_CLOSE_NEW)
        paths=(FLUSH_HEADER,SOURCE,XAM_SOURCE)
        inputs=tuple((SDK/p).read_text(encoding='utf-8') for p in paths) if SDK else (
            FLUSH_DECL,FLUSH_CPP,FLUSH_XAM+'\n'+FLUSH_CLOSE)
        header,source,xam,changed=patch_flush(*inputs)
        self.assertTrue(changed)
        for actual,expected in zip((header,source,xam),(FLUSH_DECL_NEW,FLUSH_CPP_NEW,FLUSH_XAM_NEW)):
            self.assertIn(expected,actual)
        self.assertEqual(patch_flush(header,source,xam),(header,source,xam,False))
        self.assertIn('FlushCommitFile(file)',source)
        self.assertIn(FLUSH_CLOSE_NEW,xam)
        close=xam[xam.index(FLUSH_CLOSE_NEW):][:len(FLUSH_CLOSE_NEW)]
        self.assertLess(close.index('(void)manager->FlushNativeHulkContent'),close.index('manager->CloseContent'))
        self.assertIn('MirrorOriginalProgress(',source)
        self.assertIn('root / "Headers" / "HULK.SAV.hpr"',source)
        self.assertLess(source.index('if (!flushed || !closed)'),source.index('MirrorOriginalProgress('))
        self.assertIn('global_critical_region_.Acquire()',source)
        self.assertIn('package.parent_path() != root',source)
        for index,anchor in enumerate((FLUSH_DECL,FLUSH_CPP,FLUSH_XAM)):
            broken=list(inputs);broken[index]=broken[index].replace(anchor,'missing')
            with self.assertRaises(ValueError):patch_flush(*broken)
        with self.assertRaises(ValueError):patch_flush(header,inputs[1],xam)
        with self.assertRaises(ValueError):patch_flush(inputs[0],inputs[1],inputs[2].replace(FLUSH_CLOSE,'missing'))
        with self.assertRaises(ValueError):patch_flush(header,source,xam.replace(FLUSH_CLOSE_NEW,FLUSH_CLOSE))
    def test_native_open_identity(self):
        from patch_sdk_hulk_native_save_metadata import patch_open_identity, OPEN_IDENTITY_PAIRS
        source=(SDK/SOURCE).read_text(encoding='utf-8') if SDK else '\n'.join(
            old for old,new in OPEN_IDENTITY_PAIRS)
        result,changed=patch_open_identity(source)
        self.assertTrue(changed)
        self.assertEqual(patch_open_identity(result),(result,False))
        self.assertIn('if (!native_slot) return data == pkg;',result)
        self.assertIn('hulk::save::IsHulkSaveSlot(pkg.file_name())',result)
        for old,new in OPEN_IDENTITY_PAIRS:
            self.assertIn(new,result)
            with self.assertRaises(ValueError):patch_open_identity(source.replace(old,'missing'))
            with self.assertRaises(ValueError):patch_open_identity(result.replace(new,old))
    def test_native_codec(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        if not compiler:self.skipTest('No portable C++ compiler')
        root=pathlib.Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp:
            exe=pathlib.Path(temp)/'metadata_test'
            subprocess.run([compiler,'-std=c++17','-I',str(root/'native_port/src'),
                            str(root/'ci/native_save_metadata_test.cpp'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    def test_native_reader_legacy_projection(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        if not compiler:self.skipTest('No portable C++ compiler')
        root=pathlib.Path(__file__).resolve().parents[1]
        # Compile the exact injected read branch, not a second implementation.
        from patch_sdk_hulk_native_save_metadata import READ_NEW, READ
        start=READ_NEW.index('  if (native_slot) {')
        branch=READ_NEW[start:READ_NEW.rindex(READ)]
        prefix=r'''
#include "native_save_metadata.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
struct XCONTENT_AGGREGATE_DATA {
  uint32_t device_id=0,content_type=0,title_id=0; uint64_t xuid=0;
  std::string leaf; std::u16string name;
  void set_file_name(std::string_view s){leaf=s;}
  void set_display_name(std::u16string_view s){name=s;}
};
struct Kernel {uint32_t title_id(){return 0x534507F1;}};
enum class DummyDeviceId{HDD=1};
constexpr int X_ERROR_FILE_NOT_FOUND=2,X_ERROR_SUCCESS=0;
constexpr uint32_t kCurrentlyRunningTitleId=0xffffffff;
int Read(FILE* file,size_t file_size,XCONTENT_AGGREGATE_DATA& data){
  constexpr size_t header_size=328;
  const bool native_slot=true;
  const uint32_t title_id=kCurrentlyRunningTitleId,content_type=1;
  const uint64_t xuid=42; const std::string_view file_name="HULK.SAV";
  Kernel kernel; auto* kernel_state_=&kernel;
'''
        suffix=r'''
  return 99;
}
void check(bool b){if(!b)std::abort();}
int run(const std::vector<uint8_t>& bytes,size_t size,XCONTENT_AGGREGATE_DATA& data){
  auto f=std::tmpfile();check(f);
  check(fwrite(bytes.data(),1,bytes.size(),f)==bytes.size());
  rewind(f);return Read(f,size,data);
}
int main(){
  std::vector<uint8_t> legacy(328);
  legacy[7]=1;legacy[8]=4;legacy[9]=0x10;
  memcpy(legacy.data()+264,"HULK.SAV",8);
  memset(legacy.data()+312,0xff,12);
  XCONTENT_AGGREGATE_DATA d;
  check(run(legacy,328,d)==0&&d.xuid==42&&d.title_id==0x534507F1&&d.name==u"\u0410");
  auto native=*hulk::save::EncodeNativeMetadata(u"\u0410");
  check(run(native,native.size(),d)==0&&d.name==u"\u0410");
  auto bad=legacy;bad[7]=2;check(run(bad,328,d)==2);
  bad=legacy;bad[264]='X';check(run(bad,328,d)==2);
  bad=legacy;bad[8]=0xd8;bad[9]=0;check(run(bad,328,d)==2);
  bad=legacy;for(size_t i=8;i<264;i++)bad[i]=1;check(run(bad,328,d)==2);
  bad=legacy;bad.resize(332);check(run(bad,332,d)==2);
  check(run(legacy,327,d)==2);check(run(legacy,329,d)==2);
  check(run(native,native.size()-1,d)==2);check(run(native,native.size()+1,d)==2);
  for(size_t i=0;i<native.size();i++){
    bad=native;bad[i]^=1;check(run(bad,bad.size(),d)==2);
  }
}
'''
        with tempfile.TemporaryDirectory() as temp:
            source=pathlib.Path(temp)/'reader.cpp'
            exe=pathlib.Path(temp)/('reader.exe' if sys.platform=='win32' else 'reader')
            source.write_text(prefix+branch+suffix,encoding='utf-8')
            subprocess.run([compiler,'-std=c++17','-I',str(root/'native_port/src'),
                            str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
if __name__=='__main__':unittest.main()
