"""Synthetic fail-closed tests; never require or distribute the proprietary game."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "verify_original_exe.py"
spec = importlib.util.spec_from_file_location("verify_original_exe", SCRIPT)
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


def synthetic_pe(*, peoff=0x40, machine=0x14c, magic=0x10b,
                 timestamp=verifier.EXPECTED_TIMESTAMP,
                 entry=verifier.EXPECTED_ENTRY,
                 image_size=verifier.EXPECTED_IMAGE_SIZE):
    data = bytearray(512)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, peoff)
    if peoff + 0x78 <= len(data):
        data[peoff:peoff + 4] = b"PE\x00\x00"
        struct.pack_into("<HHI", data, peoff + 4, machine, 3, timestamp)
        struct.pack_into("<H", data, peoff + 20, 0xe0)
        struct.pack_into("<H", data, peoff + 24, magic)
        struct.pack_into("<I", data, peoff + 40, entry)
        struct.pack_into("<I", data, peoff + 80, image_size)
    return bytes(data)


class OriginalExeVerificationTests(unittest.TestCase):
    def check(self, payload, *, match_digest=True):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "HULK.EXE"
            path.write_bytes(payload)
            digest = hashlib.sha256(payload).hexdigest()
            with patch.object(verifier, "EXPECTED_SHA256",
                              digest if match_digest else "0" * 64):
                result = verifier.verify(path)
            self.assertEqual(path.read_bytes(), payload, "verification modified the file")
            return result

    def test_matching_synthetic_identity(self):
        ok, errors = self.check(synthetic_pe())
        self.assertTrue(ok, errors)

    def test_wrong_sha_fails_even_when_pe_fields_match(self):
        ok, errors = self.check(synthetic_pe(), match_digest=False)
        self.assertFalse(ok)
        self.assertTrue(any("SHA256" in error for error in errors))

    def test_wrong_machine_fails_even_with_matching_digest(self):
        ok, errors = self.check(synthetic_pe(machine=0x8664))
        self.assertFalse(ok)
        self.assertTrue(any("PE32/i386" in error for error in errors))

    def test_wrong_optional_magic(self):
        ok, _ = self.check(synthetic_pe(magic=0x20b))
        self.assertFalse(ok)

    def test_wrong_identity_fields(self):
        for field in ("timestamp", "entry", "image_size"):
            with self.subTest(field=field):
                ok, errors = self.check(synthetic_pe(**{field: 1}))
                self.assertFalse(ok)
                self.assertTrue(any("identity mismatch" in e for e in errors))

    def test_bad_signature(self):
        data = bytearray(synthetic_pe())
        data[0x40:0x44] = b"NOPE"
        ok, errors = self.check(bytes(data))
        self.assertFalse(ok)
        self.assertTrue(any("PE signature" in e for e in errors))

    def test_truncated_and_invalid_offsets(self):
        for data in (b"", b"MZ", b"MZ" + bytes(62),
                     synthetic_pe(peoff=0xfffffff0)):
            with self.subTest(length=len(data)):
                ok, _ = self.check(data)
                self.assertFalse(ok)

    def test_truncated_section_table_rejected_even_with_matching_digest(self):
        # Synthetic PE has three section headers; the third must be present.
        data = synthetic_pe()[:420]
        ok, errors = self.check(data)
        self.assertFalse(ok)
        self.assertTrue(any("section table" in e for e in errors))

    def test_impossible_optional_header_length_rejected(self):
        data = bytearray(synthetic_pe())
        struct.pack_into("<H", data, 0x40 + 20, 0xFFFF)
        ok, errors = self.check(bytes(data))
        self.assertFalse(ok)
        self.assertTrue(any("section table" in e for e in errors))

    def test_verifier_opens_executable_only_once(self):
        payload = synthetic_pe()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "HULK.EXE"
            path.write_bytes(payload)
            real_open = Path.open
            opened = []
            def tracked_open(self, *args, **kwargs):
                if self == path:
                    opened.append(1)
                return real_open(self, *args, **kwargs)
            with patch.object(verifier, "EXPECTED_SHA256", hashlib.sha256(payload).hexdigest()):
                with patch.object(Path, "open", tracked_open):
                    ok, errors = verifier.verify(path)
            self.assertTrue(ok, errors)
            self.assertEqual(len(opened), 1, "hash and PE parse must use one file handle")

    def test_missing_path(self):
        with tempfile.TemporaryDirectory() as directory:
            ok, errors = verifier.verify(Path(directory) / "missing.exe")
        self.assertFalse(ok)
        self.assertTrue(any("Cannot inspect" in e for e in errors))


if __name__ == "__main__":
    unittest.main()
