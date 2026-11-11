#!/usr/bin/env python3
"""Execute extracted C++ overlap predicates against an independent byte-set oracle.

Optional SDK_ROOT validates and tests the actual dependency source. No game
assets or GPU are required; cache locking and bucket linkage are not modeled.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from patch_sdk_hulk_index_cache import FILE, OLD, NEW, patch

SDK = Path(sys.argv.pop(1)) if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else None


class IndexCacheTests(unittest.TestCase):
    def test_atomic_idempotent(self):
        self.assertEqual(patch(OLD), (NEW, True))
        self.assertEqual(patch(NEW), (NEW, False))
        for bad in ('drift', OLD + OLD, 'HULK/INDEX-CACHE-OVERLAP'):
            with self.assertRaises(ValueError):
                patch(bad)

    def test_compiled_overlap_regression(self):
        source = (SDK / FILE).read_text(encoding='utf-8-sig') if SDK else OLD
        fixed, _ = patch(source)
        def condition(body):
            start = body.index('          if (entry_key.base < physical_address_end)')
            return re.findall(r'if \(([^\n]+)\) \{', body[start:])[:2]
        old = condition(OLD)
        new = condition(fixed)
        def function(name, conditions):
            return ('bool '+name+'(uint32_t b,uint32_t entry_end,uint32_t physical_address_start,'
                    'uint32_t physical_address_end) { struct {uint32_t base;} entry_key{b}; return ('+
                    ') && ('.join(conditions)+'); }\n')
        code = '#include <cstdint>\n#include <cassert>\n' + function('before', old) + function('after', new)
        code += r'''
int main() {
  // Exact rewrite, containing write, same end, and either partial overlap.
  assert(!before(100,120,100,120) && after(100,120,100,120));
  assert(!before(100,120,90,130) && after(100,120,90,130));
  assert(!before(100,120,110,120) && after(100,120,110,120));
  assert(after(100,120,90,110) && after(100,120,110,130));
  assert(!after(100,120,80,100) && !after(100,120,120,140));
  // A real read after a same-address rewrite must recompute the conversion.
  int cached_first_index=3, guest_first_index=9;
  if (after(100,120,100,120)) cached_first_index=guest_first_index;
  assert(cached_first_index==9);
  // Independent oracle: enumerate byte addresses, not interval inequalities.
  for (uint32_t b=0;b<16;++b) for(uint32_t e=b+1;e<=16;++e)
    for(uint32_t w=0;w<16;++w) for(uint32_t z=w+1;z<=16;++z) {
      bool shared=false;
      for(uint32_t i=b;i<e;++i) for(uint32_t j=w;j<z;++j) shared |= i==j;
      assert(after(b,e,w,z)==shared);
    }
  assert(after(0x1ffffff0u,0x20000000u,0x1ffffff0u,0x20000000u));
}
'''
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'C++ compiler required')
        with tempfile.TemporaryDirectory(prefix='hulk_index_overlap_') as d:
            cpp=Path(d)/'overlap.cpp'; exe=Path(d)/'overlap.exe'
            cpp.write_text(code,encoding='utf-8')
            subprocess.run([compiler,'-std=c++17','-O0',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    unittest.main()
