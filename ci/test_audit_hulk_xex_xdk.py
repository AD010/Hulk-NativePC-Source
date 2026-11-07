#!/usr/bin/env python3
"""Synthetic XEX2 unit tests; never checks in proprietary game bytes."""
import struct
import unittest
from audit_hulk_xex_xdk import inspect, verify_xdk_6534


def make_xex(build=6534, title=0x534507F1):
    d = bytearray(0x4000)
    d[:4] = b'XEX2'
    struct.pack_into('>IIII', d, 0x8, 0x4000, 0, 0x90, 2)
    struct.pack_into('>II', d, 0x18, 0x200FF, 0x250)
    struct.pack_into('>II', d, 0x20, 0x40006, 0x350)
    struct.pack_into('>I', d, 0x250, 4+16*2)
    for n, name in enumerate((b'D3D9', b'XGRAPHC')):
        p = 0x254 + 16*n
        d[p:p+8] = name.ljust(8,b'\x00')
        struct.pack_into('>HHHH', d, p+8, 2, 0, build, 0x4001)
    struct.pack_into('>IIII', d, 0x350, 0x2BA1D2F6, 2, 2, title)
    return bytes(d)


class TestXex2Xdk(unittest.TestCase):
    def test_valid_6534(self):
        result = inspect(make_xex())
        verify_xdk_6534(result)
        self.assertEqual(result['title_id'], '534507F1')
        self.assertEqual(result['media_id'], '2BA1D2F6')
        self.assertEqual(result['static_libraries'][0]['version'], '2.0.6534')

    def test_rejects_wrong_version(self):
        with self.assertRaisesRegex(ValueError, 'D3D9 library version'):
            verify_xdk_6534(inspect(make_xex(build=7978)))

    def test_rejects_wrong_title_and_truncated(self):
        with self.assertRaisesRegex(ValueError, 'Wrong title'):
            verify_xdk_6534(inspect(make_xex(title=0x12345678)))
        with self.assertRaisesRegex(ValueError, 'Not an Xbox 360 XEX2'):
            inspect(make_xex()[:10])

    def test_rejects_invalid_static_libs(self):
        d = bytearray(make_xex())
        struct.pack_into('>I', d, 0x250, 9999999)
        with self.assertRaisesRegex(ValueError, 'Malformed'):
            inspect(bytes(d))


if __name__ == '__main__':
    unittest.main()
