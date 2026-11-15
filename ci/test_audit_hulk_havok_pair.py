#!/usr/bin/env python3
"""No proprietary data: synthetic PC/USA Havok index/packfile regression."""
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from test_audit_hulk_pc_x360_ui_assets import make_index
from audit_hulk_havok_pair import audit, audit_archive, MAGIC, VERSION

def pack(key,order):
    b=bytearray(400);m=24;b[m:m+8]=MAGIC
    struct.pack_into(order+'II',b,m+8,key,5)
    b[m+16:m+20]=(b'\x04\x01\x00\x01' if order=='<' else b'\x04\x00\x00\x01')
    struct.pack_into(order+'I',b,m+20,3)
    b[m+40:m+40+len(VERSION)]=VERSION
    for i,name in enumerate((b'__classnames__',b'__data__',b'__types__')):
        p=m+64+i*48;b[p:p+len(name)]=name;b[p+19]=0xff
        if i==0:
            section=(208,16,16,16,16,16,16)
        elif i==1:
            section=(224,16,32,48,64,64,64)
        else:
            section=(288,0,0,0,0,0,0)
        struct.pack_into(order+'7I',b,p+20,*section)
    data=m+224
    struct.pack_into(order+'II',b,data+16,0,8)
    struct.pack_into(order+'II',b,data+24,0xffffffff,0xffffffff)
    struct.pack_into(order+'III',b,data+32,4,1,8)
    struct.pack_into(order+'I',b,data+44,0xffffffff)
    struct.pack_into(order+'III',b,data+48,8,0,0)
    struct.pack_into(order+'I',b,data+60,0xffffffff)
    return bytes(b)

def archive(key,order):
    data=pack(key,order)
    return struct.pack('<I',len(data)+4)+data+b'\x00'*9

class Tests(unittest.TestCase):
    def test_paired_archives_and_wrong_platform_fail_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);data={}
            keys={'Havoks':0x12345678,'HavokMopps':0x9A001234}
            for platform,order in (('pc','<'),('xbox','>')):
                index=make_index([(name,[(key,4,400,0)])
                                  for name,key in keys.items()],order)
                (root/(platform+'.ind')).write_bytes(index)
                for name,key in keys.items():
                    path=root/(platform+name+'.arc')
                    path.write_bytes(archive(key,order))
                    data[(platform,name)]=path
            digest=hashlib.sha256((root/'xbox.ind').read_bytes()).hexdigest()
            args=(root/'pc.ind',root/'xbox.ind',data[('pc','Havoks')],
                  data[('pc','HavokMopps')],data[('xbox','Havoks')],
                  data[('xbox','HavokMopps')])
            result=audit(*args,xbox_index_sha=digest)
            self.assertEqual(result['totals'],{'pc':2,'xbox':2})
            self.assertEqual(result['original_havok_assets']['Havoks']['shared_ids'],1)
            self.assertFalse(result['hkpWorld_timestep_address_verified'])
            self.assertFalse(result['native_havok_deserialization_implemented'])
            self.assertEqual(result['original_havok_assets']['Havoks']
                             ['pc']['validated_fixup_edges'],
                             {'local':1,'global':1,'virtual':1})
            with self.assertRaises(ValueError):
                audit(*args,xbox_index_sha='0'*64)
            with self.assertRaises(ValueError):
                audit_archive(data[('pc','Havoks')],
                              [{'hash':'12345677','offset':4,'size':400,'reserved':0}],'PC')
            with self.assertRaises(ValueError):
                audit_archive(data[('pc','Havoks')],
                              [{'hash':'12345678','offset':4,'size':400,'reserved':0}],'Xbox360')
            corrupt=bytearray(data[('xbox','Havoks')].read_bytes())
            corrupt[4+24+40]=0
            data[('xbox','Havoks')].write_bytes(corrupt)
            with self.assertRaises(ValueError):
                audit(*args,xbox_index_sha=digest)

if __name__=='__main__':
    unittest.main()
