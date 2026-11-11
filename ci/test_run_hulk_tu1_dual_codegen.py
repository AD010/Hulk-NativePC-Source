#!/usr/bin/env python3
"""Synthetic-only staging/codegen checks; NO real XEX or TU1 needed."""
import os
from pathlib import Path
import tempfile
import unittest
from run_hulk_tu1_dual_codegen import manifest_text, prepare_codegen, codegen


class DualCodegenTests(unittest.TestCase):
    def test_manifest_never_uses_usa_function_hints(self):
        value = manifest_text()
        self.assertIn("includes = []", value)
        self.assertIn('file_path = "game/default.xex"', value)
        self.assertNotIn("missing_funcs.toml", value)
        self.assertNotIn("indirect_funcs.toml", value)

    def test_baseline_cannot_inherit_eu_xexp(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            xex = root / "private.xex"
            patch = root / "private.xexp"
            xex.write_bytes(b"SYNTHETIC XEX2")
            patch.write_bytes(b"SYNTHETIC XEXP")
            base = prepare_codegen(root / "baseline", xex)
            updated = prepare_codegen(root / "updated", xex, patch)
            self.assertFalse((base.parent / "game/default.xexp").exists())
            self.assertEqual((updated.parent / "game/default.xexp").read_bytes(),
                             b"SYNTHETIC XEXP")
            self.assertEqual((base.parent / "game/default.xex").read_bytes(),
                             b"SYNTHETIC XEX2")
            with self.assertRaises(FileExistsError):
                prepare_codegen(root / "updated", xex, patch)

    def test_codegen_process_and_missing_executable_fail_closed(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            xex = root / "synthetic.xex"
            xex.write_bytes(b"SYNTHETIC XEX2")
            manifest = prepare_codegen(root / "baseline", xex)
            with self.assertRaisesRegex(RuntimeError, "Cannot launch"):
                codegen(root / "nonexistent-rexglue", manifest)
            if os.name != "nt":
                fake = root / "fake-rexglue"
                fake.write_text('#!/usr/bin/env python3\n'
                                'from pathlib import Path\n'
                                'p=Path("generated/default")\n'
                                'p.mkdir(parents=True)\n'
                                '(p/"test_recomp.0.cpp").write_text("// synthetic")\n',
                                encoding="utf-8")
                fake.chmod(0o755)
                self.assertEqual(codegen(fake, manifest),
                                 manifest.parent / "generated/default")


if __name__ == "__main__":
    unittest.main()
