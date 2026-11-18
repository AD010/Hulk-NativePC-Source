"""Synthetic checks for the original PC EXE hidden-option string auditor."""
import importlib.util
import hashlib
import tempfile
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'audit_hidden_options',
    Path(__file__).resolve().parents[1] / 'tools' / 'audit_hidden_options.py')
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class AuditTests(unittest.TestCase):
    def test_ascii_and_utf16_offsets(self):
        data = b'prefix -noshadows\0' + '-novsync'.encode('utf-16le')
        result = mod.audit(data)
        self.assertEqual(result['noshadows'], [('ascii', 7)])
        self.assertEqual(result['novsync'], [('utf16le', 18)])

    def test_no_partial_switch_matches(self):
        self.assertEqual(mod.audit(b'-noshadowsExtra xx-novsyncX'), {})

    def test_double_hyphen_is_not_a_standalone_switch(self):
        # A substring of --noshadows is not the exact -noshadows token.
        self.assertEqual(mod.audit(b'--noshadows --novsync'), {})
        self.assertEqual(mod.audit('--noshadows --novsync'.encode('utf-16le')), {})
        self.assertEqual(mod.audit(b' -noshadows\x00')['noshadows'], [('ascii', 1)])

    def test_utf16_partial_switch_matches(self):
        # Both neighboring UTF-16LE code units must be inspected.
        self.assertEqual(mod.audit('-noshadowsExtra -novsyncX'.encode('utf-16le')), {})
        self.assertEqual(mod.audit('prefix_noshadows'.encode('utf-16le')), {})
        self.assertEqual(mod.audit('X-noshadows'.encode('utf-16le')), {})
        self.assertEqual(mod.audit(' -noshadows '.encode('utf-16le'))['noshadows'],
                         [('utf16le', 2)])

    def test_pe_header_bounds_reject_forged_optional_header(self):
        # Synthetic PE32/i386; never uses or distributes the game's EXE.
        def check(optional_size, sections, length):
            data = bytearray(length)
            data[:2] = b'MZ'
            data[0x3c:0x40] = (0x80).to_bytes(4, 'little')
            data[0x80:0x84] = b'PE\x00\x00'
            data[0x84:0x86] = (0x14c).to_bytes(2, 'little')
            data[0x86:0x88] = sections.to_bytes(2, 'little')
            data[0x94:0x96] = optional_size.to_bytes(2, 'little')
            data[0x98:0x9a] = (0x10b).to_bytes(2, 'little')
            with tempfile.TemporaryDirectory() as folder:
                exe = Path(folder) / 'HULK.EXE'
                exe.write_bytes(data)
                original = mod.EXPECTED_SHA256
                mod.EXPECTED_SHA256 = hashlib.sha256(data).hexdigest()
                try:
                    return mod.main([str(exe)])
                finally:
                    mod.EXPECTED_SHA256 = original

        self.assertEqual(check(0, 0, 256), 2)
        self.assertEqual(check(0xe0, 0, 256), 2)
        self.assertEqual(check(2, 4, 256), 2)
        self.assertEqual(check(2, 0, 256), 2)
        self.assertEqual(check(0xe0, 0, 512), 2)
        self.assertEqual(check(0xe0, 1, 512), 0)

    def test_section_mapping_does_not_confuse_overlay_with_image(self):
        data = bytearray(512)
        pe_offset, optional_size = 0x80, 0x60
        entry = pe_offset + 24 + optional_size
        data[entry:entry + 8] = b'.rdata\\0\\0'
        data[entry + 16:entry + 20] = (64).to_bytes(4, 'little')
        data[entry + 20:entry + 24] = (256).to_bytes(4, 'little')
        self.assertEqual(mod.section_for_offset(data, pe_offset, optional_size, 1, 256), '.rdata')
        self.assertEqual(mod.section_for_offset(data, pe_offset, optional_size, 1, 319), '.rdata')
        self.assertEqual(mod.section_for_offset(data, pe_offset, optional_size, 1, 320), '<headers-or-overlay>')
        self.assertEqual(mod.section_for_offset(data, pe_offset, optional_size, 1, 128), '<headers-or-overlay>')

    def test_missing_executable_is_rejected_without_traceback(self):
        with tempfile.TemporaryDirectory() as folder:
            self.assertEqual(mod.main([str(Path(folder) / 'missing-HULK.EXE')]), 2)

    def test_empty_input(self):
        self.assertEqual(mod.audit(b''), {})


if __name__ == '__main__':
    unittest.main()
