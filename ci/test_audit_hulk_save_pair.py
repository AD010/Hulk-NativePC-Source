#!/usr/bin/env python3
"""Synthetic save samples only; never commit the owner's real game data."""
import unittest
import zlib
from audit_hulk_save_pair import validate, compare


def fake_save(keys,values,order,offset):
    b=bytearray(offset+8*len(keys)+24)
    for i,(key,value) in enumerate(zip(keys,values)):
        pos=offset+8*i
        b[pos:pos+4]=key.to_bytes(4,order)
        b[pos+4:pos+8]=value.to_bytes(4,order)
    crc=zlib.crc32(b[12:])&0xffffffff
    b[:4]=crc.to_bytes(4,order)
    return bytes(b)


class SaveAuditTests(unittest.TestCase):
    def test_valid_crc_both_byte_orders_and_shared_schema(self):
        keys=[0x11223344,0x55667788,0x99AABBCC]
        xbox=fake_save(keys,[11,22,33],"big",20)
        pc=fake_save(keys,[11,77,33],"little",16)
        report=compare(xbox,pc)
        self.assertTrue(report["xbox_recomp"]["crc32_verified"])
        self.assertTrue(report["windows_2008"]["crc32_verified"])
        self.assertEqual(report["shared_leading_hashed_field_ids"],3)
        self.assertEqual(report["identical_values_among_shared_ids"],2)
        self.assertFalse(report["conversion_ready"])

    def test_mutation_or_wrong_endian_is_rejected(self):
        s=fake_save([0x11223344],[4],"big",20)
        bad=bytearray(s);bad[-1]^=0x80
        with self.assertRaisesRegex(ValueError,"CRC32"):
            validate(bytes(bad),"big")
        with self.assertRaisesRegex(ValueError,"CRC32"):
            validate(s,"little")

    def test_mismatched_ids_stop_common_prefix(self):
        x=fake_save([0x11223344,0x55667788],[1,2],"big",20)
        pc=fake_save([0x11223344,0x55667799],[1,2],"little",16)
        self.assertEqual(compare(x,pc)["shared_leading_hashed_field_ids"],1)

    def test_truncated_or_invalid_format_rejected(self):
        with self.assertRaises(ValueError): validate(b"short","big")
        with self.assertRaises(ValueError): validate(b"X"*64,"other")


if __name__=="__main__":
    unittest.main()
