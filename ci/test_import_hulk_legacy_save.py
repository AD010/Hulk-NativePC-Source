#!/usr/bin/env python3
"""Migration checks using synthetic, checksum-verified Xbox-like saves."""
import pathlib
import tempfile
import unittest
import zlib
from import_hulk_legacy_save import (
    CONTENT_TYPE, SLOT, TITLE, find_source, import_once, plan, verify_legacy,
)


def fake_payload() -> bytes:
    data=bytearray(256)
    data[12:16]=b"EMX "
    data[20:24]=(0x00678856).to_bytes(4,"big")
    data[:4]=(zlib.crc32(data[12:])&0xffffffff).to_bytes(4,"big")
    return bytes(data)


class MigrationSafetyTests(unittest.TestCase):
    def stage_original(self, directory: pathlib.Path):
        root=directory/"legacy"
        base=root/"B13EBABEBABEBABE"/TITLE
        payload=base/CONTENT_TYPE/SLOT/SLOT
        header=base/"Headers"/CONTENT_TYPE/(SLOT+".header")
        payload.parent.mkdir(parents=True)
        header.parent.mkdir(parents=True)
        original=fake_payload()
        payload.write_bytes(original)
        header.write_bytes(b"\x00\x00\x00\x01"*2+bytes(320))
        return root,original,payload

    def test_copy_and_reimport_refusal(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory=pathlib.Path(tmp)
            root,original,original_path=self.stage_original(directory)
            report=plan(root,directory/"Documents")
            self.assertTrue(report["crc32_verified"])
            self.assertFalse(report["target_exists"])
            imported=import_once(report)
            target=pathlib.Path(imported["destination"])
            self.assertEqual((target/SLOT/SLOT).read_bytes(),original)
            self.assertEqual((target/"Headers"/(SLOT+".header")).stat().st_size,328)
            self.assertEqual(original_path.read_bytes(),original)
            self.assertFalse((directory/"Documents/SEGA/The Incredible Hulk/HULK.sav").exists())
            with self.assertRaises(FileExistsError):
                import_once(report)

    def test_corrupt_payload_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root,original,payload=self.stage_original(pathlib.Path(tmp))
            bad=bytearray(original);bad[-1]^=1;payload.write_bytes(bad)
            with self.assertRaisesRegex(ValueError,"CRC32"):
                plan(root,pathlib.Path(tmp)/"Documents")

    def test_invalid_header_and_multiple_users_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=pathlib.Path(tmp)
            root,_,_=self.stage_original(base)
            header=next(root.rglob("HULK.SAV.header"))
            header.write_bytes(b"bad")
            with self.assertRaisesRegex(ValueError,"XContent aggregate"):
                plan(root,base/"Documents")
            header.write_bytes(b"\x00\x00\x00\x01"*2+bytes(320))
            another=root/"1234567812345678"/TITLE
            p=another/CONTENT_TYPE/SLOT/SLOT
            h=another/"Headers"/CONTENT_TYPE/(SLOT+".header")
            p.parent.mkdir(parents=True)
            h.parent.mkdir(parents=True)
            p.write_bytes(fake_payload())
            h.write_bytes(header.read_bytes())
            with self.assertRaisesRegex(ValueError,"exactly one"):
                find_source(root)


if __name__ == "__main__":
    unittest.main()
