"""Synthetic checks for the original PC EXE hidden-option string auditor."""
import importlib.util
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

    def test_empty_input(self):
        self.assertEqual(mod.audit(b''), {})


if __name__ == '__main__':
    unittest.main()
