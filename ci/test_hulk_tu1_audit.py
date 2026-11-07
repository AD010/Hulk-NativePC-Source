#!/usr/bin/env python3
"""Tests for Xbox STFS/XEXP and release ZIP guards. No retail game content needed."""
import hashlib
import tempfile
import unittest
import zipfile
from pathlib import Path

from audit_hulk_tu1 import data_offset, hash_offset, xex_rsa_digest, xexp_source
from verify_hulk_release import CORE, verify


class TestHulkTU1Audit(unittest.TestCase):
    def test_stfs_offsets(self):
        self.assertEqual(data_offset(0), 0xC000)
        self.assertEqual(data_offset(169), 0xB5000)
        self.assertEqual(data_offset(170), 0xB8000)
        self.assertEqual(data_offset(339), 0x161000)
        self.assertEqual(data_offset(340), 0x163000)
        self.assertEqual(data_offset(510), 0x20E000)
        self.assertEqual(hash_offset(0), 0xB000)
        self.assertEqual(hash_offset(170), 0xB7000)
        self.assertEqual(hash_offset(340), 0x162000)

    def test_xex_signature_digest(self):
        xex = bytearray(0x300)
        xex[:4] = b"XEX2"
        xex[0x10:0x14] = (0x100).to_bytes(4, "big")
        xex[0x108:0x208] = bytes(range(256))
        self.assertEqual(xex_rsa_digest(xex), hashlib.sha1(bytes(range(256))).digest())

    def test_xexp_delta_descriptor(self):
        xexp = bytearray(0x180)
        xexp[:4] = b"XEX2"
        xexp[0x14:0x18] = (1).to_bytes(4, "big")
        xexp[0x18:0x1C] = (0x5FF).to_bytes(4, "big")
        xexp[0x1C:0x20] = (0x40).to_bytes(4, "big")
        xexp[0x44:0x48] = (0x102).to_bytes(4, "big")
        xexp[0x48:0x4C] = (2).to_bytes(4, "big")
        xexp[0x4C:0x60] = b"A" * 20
        self.assertEqual(xexp_source(xexp), (b"A" * 20, 2, 0x102))

    def test_release_accepts_exact_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "game.zip"
            with zipfile.ZipFile(path, "w") as z:
                for name in CORE:
                    z.writestr(name, b"test")
            self.assertEqual(set(verify(path)), CORE)

    def test_release_rejects_developer_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "game.zip"
            with zipfile.ZipFile(path, "w") as z:
                for name in CORE | {"DEBUG.cmd"}:
                    z.writestr(name, b"test")
            with self.assertRaises(ValueError):
                verify(path)


if __name__ == "__main__":
    unittest.main()
