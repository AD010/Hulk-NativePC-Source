#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import struct
import unittest
P=Path(__file__).resolve().parents[1]/'tools'/'eor_ind_endian_bridge.py'
spec=importlib.util.spec_from_file_location('eor_bridge',P)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
class Translation(unittest.TestCase):
 def sample(self,order):
  n=2; out=bytearray(4+4*3); off=[len(out)]
  out+=b'Models\0\0';off.append(len(out))
  out+=struct.pack(order+'I',1)
  out+=struct.pack(order+'I',0x1289abcf)
  out+=struct.pack(order+'III',4,256,0)
  off.append(len(out))
  out+=struct.pack(order+'I',1000)
  struct.pack_into(order+'I',out,0,n)
  struct.pack_into(order+'3I',out,4,*off)
  return bytes(out)
 def test_roundtrip_and_wrong_footer(self):
  for src,dst in (('<','>'),('>','<')):
   raw=self.sample(src);changed=module.convert(raw,src,dst)
   self.assertNotEqual(raw,changed)
   self.assertEqual(module.convert(changed,dst,src),raw)
   wrong=bytearray(raw);struct.pack_into(src+'I',wrong,len(raw)-4,13)
   with self.assertRaises(ValueError):module.convert(bytes(wrong),src,dst)
if __name__=='__main__':unittest.main()
