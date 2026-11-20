#!/usr/bin/env python3
"""Synthetic TU1 staging tests; optional real TU1 check via environment vars."""
import hashlib
import os
import pathlib
import struct
import tempfile
import unittest
import zlib
from unittest.mock import patch
import prepare_hulk_tu1_stage as source
from prepare_hulk_tu1_stage import arrange, stage, export_decoded_overrides


class TestTU1Staging(unittest.TestCase):
    def test_unknown_archive_refused_without_creating_output(self):
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d)
            bad = p / 'bad.zip'
            bad.write_bytes(b'not a signed TU archive')
            xex = p / 'default.xex'
            xex.write_bytes(b'bad XEX')
            with self.assertRaises(ValueError):
                stage(bad, xex, p / 'target')
            self.assertFalse((p / 'target').exists())

    def test_existing_target_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d)
            (p / 'target').mkdir()
            with self.assertRaises(FileExistsError):
                stage(p / 'bad.zip', p / 'xex', p / 'target')

    def test_invalid_inventory_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d)
            with self.assertRaisesRegex(ValueError, 'inventory'):
                arrange(p, p / 'xex', p / 'output',
                        {'files': [{'path': 'default.xexp'}]})


    def test_eu_tu1_decoded_resources_and_corrupt_archive_rejected(self):
        # Small synthetic signed Models index with an authentic raw-DEFLATE
        # local block, never containing any retail shader or model bytes.
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            data = b'synthetic_european_model'
            compressor = zlib.compressobj(wbits=-15)
            compressed = compressor.compress(data) + compressor.flush()
            local = struct.pack('<IHHHHHIIIHH', 0x04034B50,
                                0, 0, 8, 0, 0, 0, len(compressed),
                                len(data), 0, 0) + compressed
            wrapper = struct.pack('>III', len(data), 0x10000,
                                  len(local)) + local
            archive = b'\0' * 4 + wrapper
            key = 0x12345678
            group = b'Models\0'
            first = 16
            table = first + len(group)
            end = table + 20
            index = struct.pack('>4I', 2, first, table, end) + group + (
                struct.pack('>i4I', -1, key, 4, len(wrapper), 0))
            payloads = {
                'index.ind': index, 'default.xexp': b'SYNTHETIC_XEXP',
                'MODELS.ARC': archive,
            }
            for name, value in payloads.items():
                (root / name).write_bytes(value)
            manifest = {
                'xex_signature_match': True,
                'xexp_source_rsa_sha1': source.EXPECTED_TU_SOURCE_SIGNATURE,
                'files': [{'path': k, 'size': len(v),
                           'sha256': hashlib.sha256(v).hexdigest()}
                          for k, v in payloads.items()],
            }
            opts = {
                'EXPECTED_TU_FILE_COUNT': 3,
                'EXPECTED_TU_RECORD_COUNT': 1,
                'EXPECTED_TU_XEXP_SHA256': hashlib.sha256(
                    payloads['default.xexp']).hexdigest(),
                'EXPECTED_TU_DELTA_INDEX_SHA256': hashlib.sha256(index).hexdigest(),
                'ARCHIVE_NAMES': {'Models': 'MODELS.ARC'},
            }
            from contextlib import ExitStack
            with ExitStack() as stack:
                for k, v in opts.items():
                    stack.enter_context(patch.object(source, k, v))
                out = root / 'decoded'
                report = export_decoded_overrides(root, out, manifest)
                self.assertEqual(report['resource_count'], 1)
                self.assertEqual(report['decoded_bytes'], len(data))
                self.assertFalse(report['native_eor_resource_override_enabled'])
                self.assertEqual(
                    (out / 'resources/Models/12345678.bin').read_bytes(), data)
                with self.assertRaises(FileExistsError):
                    export_decoded_overrides(root, out, manifest)
                # A post-verification edit must refuse BEFORE publication.
                (root / 'MODELS.ARC').write_bytes(archive + b'\0')
                with self.assertRaisesRegex(ValueError, 'changed'):
                    export_decoded_overrides(root, root / 'rejected', manifest)
                self.assertFalse((root / 'rejected').exists())

    def test_matching_user_supplied_tu_if_present(self):
        archive_var = os.environ.get('HULK_TU1_ZIP_FOR_TEST')
        xex_var = os.environ.get('HULK_EU_XEX_FOR_TEST')
        if not archive_var or not xex_var:
            self.skipTest('Private EU XEX and TU1 not supplied for optional test')
        archive, xex = pathlib.Path(archive_var), pathlib.Path(xex_var)
        if not archive.is_file() or not xex.is_file():
            self.skipTest('Optional user-owned source files do not exist')
        with tempfile.TemporaryDirectory() as d:
            target = pathlib.Path(d) / 'staged'
            stage(archive, xex, target, decoded_overrides=True)
            self.assertTrue((target / 'codegen-game' / 'default.xexp').is_file())
            self.assertEqual(len(list((target / 'update').rglob('*.*'))), 30)
            decoded = target / 'decoded-update'
            self.assertTrue((decoded / 'overlay-manifest.json').is_file())
            self.assertEqual(len(list((decoded / 'resources').rglob('*.bin'))), 525)


if __name__ == '__main__':
    unittest.main()
