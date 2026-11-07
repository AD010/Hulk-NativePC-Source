#!/usr/bin/env python3
"""Synthetic TU1 staging tests; optional real TU1 check via environment vars."""
import os
import pathlib
import tempfile
import unittest
from prepare_hulk_tu1_stage import arrange, stage


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
            stage(archive, xex, target)
            self.assertTrue((target / 'codegen-game' / 'default.xexp').is_file())
            self.assertEqual(len(list((target / 'update').rglob('*.*'))), 30)


if __name__ == '__main__':
    unittest.main()
