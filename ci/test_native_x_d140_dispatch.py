#!/usr/bin/env python3
"""Prove native Windows D140 source-patch dispatcher checks the actual pin."""
import pathlib
import re
import tempfile
import unittest
from unittest.mock import patch
from native_x_d140_dispatch import D140, D135, checked_patch_path, check_renderer, run_patch

ROOT = pathlib.Path(__file__).resolve().parents[1]

class D140WindowsDispatchTests(unittest.TestCase):
    def test_pinned_renderer_checkout_fails_closed(self):
        with tempfile.TemporaryDirectory(prefix="hulk_d140_") as tmp:
            path=pathlib.Path(tmp)
            (path/"src").mkdir()
            (path/"src"/"state.cpp").write_text("// source only")
            with patch("native_x_d140_dispatch.subprocess.check_output",return_value=D140+"\n"):
                self.assertEqual(check_renderer(str(path)),path.resolve())
            with patch("native_x_d140_dispatch.subprocess.check_output",return_value=D135+"\n"):
                with self.assertRaisesRegex(ValueError,"actual NativeX checkout"):
                    check_renderer(str(path))
            (path/"src"/"state.cpp").unlink()
            with self.assertRaisesRegex(ValueError,"Missing NativeX source"):
                check_renderer(str(path))

    def test_direct_preflight_dispatch_rejects_wrong_checkout_before_import(self):
        # run_patch() is called directly by test_native_x_patch_chain.py.
        # An old pin must be rejected before the patch module can mutate it.
        with tempfile.TemporaryDirectory(prefix="hulk_d140_direct_") as tmp:
            root=pathlib.Path(tmp)
            (root/"src").mkdir()
            (root/"src"/"state.cpp").write_text("// fake fixture",encoding="utf-8")
            script=ROOT/"ci"/"patch_native_x_hulk_pm4_gap.py"
            with patch("native_x_d140_dispatch.subprocess.check_output",return_value=D135+"\\n"):
                with self.assertRaisesRegex(ValueError,"actual NativeX checkout"):
                    run_patch(script,root)
            with patch("native_x_d140_dispatch.subprocess.check_output",return_value=D140+"\\n"):
                with self.assertRaisesRegex(ValueError,"Unexpected source"):
                    run_patch(ROOT/"native_port"/"BUILD_WINDOWS.ps1",root)

    def test_script_path_cannot_escape_ci_or_load_arbitrary_source(self):
        script=ROOT/"ci"/"patch_native_x_hulk_pm4_gap.py"
        self.assertEqual(checked_patch_path(str(script)),script.resolve())
        with self.assertRaisesRegex(ValueError,"Unexpected source"):
            checked_patch_path(str(ROOT/"native_port"/"BUILD_WINDOWS.ps1"))
        with self.assertRaisesRegex(ValueError,"Unexpected source"):
            checked_patch_path(str(ROOT/"ci"/"test_hulk_pc_menu.py"))

    def test_release_workflow_remains_manual_and_main_only(self):
        # D140 source review must never silently turn a source push or a
        # migration-branch edit into a private Windows build/release.
        workflow=(ROOT/".github"/"workflows"/"hulk-free-windows.yml").read_text("utf-8")
        self.assertRegex(workflow,r"(?m)^  workflow_dispatch:\\s*$")
        self.assertNotRegex(workflow,r"(?m)^  (?:push|pull_request|schedule|workflow_run):")
        self.assertIn("github.ref == 'refs/heads/main'",workflow)
        self.assertIn("github.repository == 'AD010/Hulk-NativePC-Source'",workflow)
        self.assertIn("HULK_PINNED_XEX_SHA256:",workflow)
        self.assertIn("346A73690303F6633BFDAF9850E78793FE07CDBFB8F849B89FBEE320630B11F0",workflow)

    def test_builder_dispatches_every_reviewed_patch_in_original_order(self):
        builder=(ROOT/"native_port"/"BUILD_WINDOWS.ps1").read_text("utf-8")
        starts=builder.find("if ($NativeXLink) {",builder.find("$NativeXCommit"))
        self.assertGreater(starts,0)
        self.assertIn("HULK NATIVE GPU upstream fa19ea85",builder)
        self.assertIn("$NativeXCommit = '"+D140+"'",builder)
        lines=[line for line in builder.splitlines()
               if "Invoke-Checked 'python'" in line and
               re.search(r"ci/(?:test_)?patch_native_x_hulk_[a-z0-9_]+\.py",line)]
        self.assertEqual(len(lines),86)
        patch_lines=[line for line in lines if "'--patch'" in line]
        test_lines=[line for line in lines if "'--test'" in line]
        self.assertEqual((len(patch_lines),len(test_lines)),(45,41))
        self.assertTrue(all("ci/native_x_d140_dispatch.py" in line for line in lines))
        self.assertTrue(all(", $NxrFull)" in line for line in patch_lines))
        self.assertTrue(all("$NxrFull)" not in line for line in test_lines))
        self.assertNotIn("Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_native_x_hulk_",builder)
        self.assertIn("HULK/TRUE-RESOLVE-SCALE",builder)
        self.assertIn("HULK/DIRECT-DMA-SNAPSHOT",builder)


if __name__ == "__main__":
    unittest.main()
