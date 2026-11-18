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
            data[0x80:0x84] = b'PE\\0\\0'
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
        self.assertEqual(check(2, 0, 256), 0)

    def test_empty_input(self):
        self.assertEqual(mod.audit(b''), {})


if __name__ == '__main__':
    unittest.main()
