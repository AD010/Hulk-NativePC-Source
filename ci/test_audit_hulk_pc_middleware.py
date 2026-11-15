#!/usr/bin/env python3
"""Synthetic PE classifier tests without original 2008 code/assets."""
import struct
import unittest
from audit_hulk_pc_middleware import read_imports, audit

def pe_fixture(dlls=('fmod_event.dll','fmodex.dll')):
    data=bytearray(2048)
    data[:2]=b'MZ'
    struct.pack_into('<I',data,0x3c,0x80)
    data[0x80:0x84]=b'PE\x00\x00'
    coff=0x84
    struct.pack_into('<HHIIIHH',data,coff,0x14c,1,0,0,0,224,0)
    opt=coff+20
    struct.pack_into('<H',data,opt,0x10b)
    struct.pack_into('<II',data,opt+104,0x1100,20*(len(dlls)+1))
    section=opt+224
    struct.pack_into('<4I',data,section+8,0x400,0x1000,0x400,0x200)
    for i,name in enumerate(dlls):
        struct.pack_into('<5I',data,0x300+i*20,0,0,0,0x1180+24*i,0)
        data[0x380+24*i:0x380+24*i+len(name)+1]=name.encode()+b'\0'
    data[0x680:0x680+len(b'hkpRigidBody\0hkpWorld\0')]=b'hkpRigidBody\0hkpWorld\0'
    return bytes(data)

class TestPePcSource(unittest.TestCase):
    def test_imports_and_havok_are_not_xbox_proof(self):
        report=audit(pe_fixture())
        self.assertEqual(report['pe_import_dlls'],['fmod_event.dll','fmodex.dll'])
        self.assertTrue(report['native_fmod_event_import'])
        self.assertTrue(report['native_fmodex_import'])
        self.assertIn('hkpRigidBody',report['havok_pc_type_name_examples'])
        self.assertFalse(report['xbox_usa_backend_verified'])
    def test_invalid_data(self):
        for data in (b'',b'MZ'+b'\0'*130):
            with self.assertRaises(ValueError):read_imports(data)
        wrong=bytearray(pe_fixture())
        wrong[0x80]=0
        with self.assertRaises(ValueError):read_imports(bytes(wrong))

if __name__=='__main__':
    unittest.main()
