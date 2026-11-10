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

    def test_region_and_encrypted_basic_format_provenance(self):
        d = bytearray(make_xex())
        # Fixture derived from XEX2 field offsets, never from a guest binary.
        struct.pack_into('>II', d, 0x90 + 0x178, 0x00FF0000, 0x00000004)
        struct.pack_into('>I', d, 0x14, 3)
        struct.pack_into('>II', d, 0x28, 0x000003FF, 0x390)
        struct.pack_into('>IHH', d, 0x390, 8, 1, 1)
        result = inspect(bytes(d))
        verify_xdk_6534(result)
        self.assertEqual(result['game_region_mask'], '00FF0000')
        self.assertEqual(result['allowed_media_types'], '00000004')
        self.assertEqual(result['base_file_format'], {
            'encryption_type': 1, 'compression_type': 1,
        })
        # A different region can have the same Title ID and XDK version.
        struct.pack_into('>I', d, 0x90 + 0x178, 0x000000FF)
        self.assertEqual(inspect(bytes(d))['game_region_mask'], '000000FF')
        # Media ID zero and region-free does not establish the USA codegen.
        struct.pack_into('>I', d, 0x90 + 0x178, 0xFFFFFFFF)
        self.assertEqual(inspect(bytes(d))['game_region_mask'], 'FFFFFFFF')

    def test_invalid_security_format_is_rejected(self):
        d = bytearray(make_xex())
        struct.pack_into('>I', d, 0x14, 3)
        struct.pack_into('>II', d, 0x28, 0x000003FF, 0x390)
        struct.pack_into('>IHH', d, 0x390, 8, 1, 1)
        struct.pack_into('>I', d, 0x390, 0xFFFFFFFF)
        with self.assertRaisesRegex(ValueError, 'format length'):
            inspect(bytes(d))
        struct.pack_into('>IHH', d, 0x390, 8, 2, 1)
        with self.assertRaisesRegex(ValueError, 'Unsupported XEX2 encryption'):
            inspect(bytes(d))

    def test_rejects_invalid_static_libs(self):
        d = bytearray(make_xex())
        struct.pack_into('>I', d, 0x250, 9999999)
        with self.assertRaisesRegex(ValueError, 'Malformed'):
            inspect(bytes(d))


if __name__ == '__main__':
    unittest.main()
