#!/usr/bin/env python3
"""No game content needed: archive parser and corruption regression tests."""
import struct
import unittest
import zlib
from audit_hulk_world_visuals import decode_chunks

def zip_block(data: bytes) -> bytes:
    z = zlib.compressobj(wbits=-15)
    packed = z.compress(data)+z.flush()
    return struct.pack("<IHHHHHIIIHH",0x04034B50,0,0,8,0,0,0,len(packed),len(data),0,0)+packed

class TestHulkWorldSchema(unittest.TestCase):
    def test_eor_deflate_chain_with_separator(self):
        payload = b"header " + zip_block(b"FogNear\0FogFar\0") + b"\0\0" + zip_block(b"ZoneLowLod\0")
        plain, blocks = decode_chunks(payload)
        self.assertEqual(blocks, 2)
        self.assertIn(b"FogFar\0", plain)
        self.assertIn(b"ZoneLowLod\0", plain)
    def test_uncompressed(self):
        plain, blocks = decode_chunks(b"_engine_\0")
        self.assertEqual((plain, blocks), (b"_engine_\0", 0))
    def test_corrupt_deflate_rejected(self):
        with self.assertRaises((ValueError,zlib.error)):
            decode_chunks(b"header "+zip_block(b"abc")[:-1])

if __name__ == "__main__":
    unittest.main()
