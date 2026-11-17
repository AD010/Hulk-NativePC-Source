#!/usr/bin/env python3
"""Prove native Windows D140 source-patch dispatcher checks the actual pin."""
import pathlib
import re
import tempfile
import unittest
from unittest.mock import patch
from native_x_d140_dispatch import D140, D135, checked_patch_path, check_renderer, run_patch, reviewed_patch_plan

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
            with patch("native_x_d140_dispatch.subprocess.check_output",return_value=D135+"\n"):
                with self.assertRaisesRegex(ValueError,"actual NativeX checkout"):
                    run_patch(script,root)
            with patch("native_x_d140_dispatch.subprocess.check_output",return_value=D140+"\n"):
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
        self.assertRegex(workflow,r"(?m)^  workflow_dispatch:\s*$")
        # No temporary push escape hatch: source edits must NEVER start
        # an expensive release. Require an explicit manual dispatch after
        # all independent source tests and toolchain checks pass.
        self.assertNotRegex(
            workflow, r"(?m)^  (?:push|pull_request|schedule|workflow_run):")
        self.assertIn("github.ref == 'refs/heads/main'",workflow)
        self.assertIn("github.repository == 'AD010/Hulk-NativePC-Source'",workflow)
        self.assertIn("HULK_PINNED_XEX_SHA256:",workflow)
        self.assertIn("346A73690303F6633BFDAF9850E78793FE07CDBFB8F849B89FBEE320630B11F0",workflow)

    def test_no_leftover_release_dispatch_triggers(self):
        # A failed Release is never a reason to scatter permanent trigger
        # markers around the public source tree.
        leftovers=sorted(path.name for path in ROOT.glob(
            ".hulk-release-one-shot-*"))
        self.assertEqual(leftovers,[])

    def test_builder_dispatches_every_reviewed_patch_in_original_order(self):
        builder=(ROOT/"native_port"/"BUILD_WINDOWS.ps1").read_text("utf-8")
        starts=builder.find("if ($NativeXLink) {",builder.find("$NativeXCommit"))
        self.assertGreater(starts,0)
        self.assertIn("HULK NATIVE GPU upstream fa19ea85",builder)
        self.assertIn("$NativeXCommit = '"+D140+"'",builder)
        stages=reviewed_patch_plan(builder)
        self.assertEqual(len(stages),90)
        self.assertEqual(len(set(stages)),90)
        patches=[stage for stage in stages if stage.startswith("ci/patch_")]
        tests=[stage for stage in stages if stage.startswith("ci/test_")]
        self.assertEqual((len(patches),len(tests)),(47,43))
        self.assertTrue(all((ROOT/stage).is_file() for stage in stages))
        checked=re.findall(
            r"(?m)^\s+Invoke-HulkNativeXCheckedPatch "
            r"'patch_native_x_hulk_[a-z0-9_]+\.py'\s*$",builder)
        direct=re.findall(
            r"(?m)^\s+Invoke-HulkNativeXPatch "
            r"'(?:test_)?patch_native_x_hulk_[a-z0-9_]+\.py'\s*$",builder)
        self.assertEqual((len(checked),len(direct)),(42,6))
        self.assertIn("function Invoke-HulkNativeXCheckedPatch(",builder)
        self.assertIn('Invoke-HulkNativeXPatch ("test_" + $ScriptName)',builder)
        self.assertIn("function Invoke-HulkNativeXPatch(",builder)
        self.assertIn("ci/native_x_d140_dispatch.py",builder)
        self.assertIn("Invoke-Checked 'python' @($Dispatcher, '--patch', $Script, $NxrFull) $Source",builder)
        self.assertIn("Invoke-Checked 'python' @($Dispatcher, '--test', $Script) $Source",builder)
        self.assertNotIn("Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_native_x_hulk_",builder)
        self.assertIn("HULK/TRUE-RESOLVE-SCALE",builder)
        self.assertIn("HULK/DIRECT-DMA-SNAPSHOT",builder)


    def test_atomic_native_x_source_plan_rejects_unreviewed_edits(self):
        builder=(ROOT/"native_port"/"BUILD_WINDOWS.ps1").read_text("utf-8")
        for bad in (
            builder.replace(
                "Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_heap_guard.py'",
                "Invoke-HulkNativeXCheckedPatch 'test_patch_native_x_hulk_heap_guard.py'",1),
            builder.replace(
                "Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_heap_guard.py'",
                "Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_heap_guard_typo.py'",1),
            builder.replace(
                "Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_heap_guard.py'\n",
                "",1),
        ):
            with self.assertRaises(ValueError):
                reviewed_patch_plan(bad)

    def test_xenia_edge_sdk_backports_use_one_reviewed_module(self):
        # Three GPU bug fixes, each applied at its original dependency stage:
        # index pool, packed mip, then scalar SPIR-V after fetch/LOD fixes.
        # The old six per-fix files and their noisy CI invocations are gone.
        builder=(ROOT/"native_port"/"BUILD_WINDOWS.ps1").read_text("utf-8")
        workflow=(ROOT/".github"/"workflows"/"hulk-free-windows.yml").read_text("utf-8")
        patch=ROOT/"ci"/"patch_sdk_hulk_xenia_edge.py"
        tests=ROOT/"ci"/"test_patch_sdk_hulk_xenia_edge.py"
        self.assertTrue(patch.is_file() and tests.is_file())
        self.assertEqual(builder.count("ci/patch_sdk_hulk_xenia_edge.py"),3)
        self.assertEqual(builder.count("ci/test_patch_sdk_hulk_xenia_edge.py"),1)
        self.assertEqual(workflow.count("ci/test_patch_sdk_hulk_xenia_edge.py"),1)
        self.assertLess(builder.index("'index-pool', $Sdk"),
                        builder.index("'packed-mip', $Sdk"))
        self.assertLess(builder.index("'packed-mip', $Sdk"),
                        builder.index("'spirv-scalar', $Sdk"))
        for old in ("index_pool", "packed_mip", "spirv_scalar"):
            for kind in ("patch", "test_patch"):
                obsolete=ROOT/"ci"/("{}_sdk_hulk_{}.py".format(kind,old))
                self.assertFalse(obsolete.exists(), "Superseded SDK patch: "+str(obsolete))
        source=patch.read_text("utf-8")
        checks=tests.read_text("utf-8")
        for name in ("patch_index", "patch_mip", "patch_scalar",
                     "apply_index", "apply_mip", "apply_scalar"):
            self.assertIn("def "+name+"(",source)
        self.assertEqual(len(re.findall(r"(?m)^class \w+Tests\(unittest\.TestCase\):",checks)),3)
        self.assertEqual(len(re.findall(r"(?m)^\s+def test_\w+\(",checks)),9)

    def test_release_cpp_preflight_has_real_unique_inputs(self):
        # Past releases reached cl.exe only to fail on missing source units.
        # Fail in the public-only preflight, before any private game material.
        workflow=(ROOT/".github"/"workflows"/"hulk-free-windows.yml").read_text("utf-8")
        pattern=r"(?m)^\s+\$(\w+Args)\s*=\s*@\(([\s\S]*?)\)\s*\n\s+& cl\.exe @\1\s*$"
        invocations=list(re.finditer(pattern,workflow))
        self.assertGreaterEqual(len(invocations),30)
        by_name={}
        for match in invocations:
            name,body=match.group(1,2)
            with self.subTest(compiler_list=name):
                paths=re.findall(
                    r"'(native_port/src/[^']+\.cpp|ci/[^']+\.cpp)'",
                    body)
                self.assertTrue(paths)
                self.assertEqual(len(paths),len(set(paths)),
                                 "Repeated .cpp object in cl.exe link: "+name)
                for path in paths:
                    self.assertTrue((ROOT/path).is_file(),
                                    "Missing source unit in "+name+": "+path)
            by_name[name]=set(paths)
        # OriginalModelAssets calls both functions; earlier MSVC LNK2019
        # was caused by compiling it without their implementation units.
        self.assertIn("modelAssetsArgs",by_name)
        self.assertTrue({
            "native_port/src/eor_model_surface_materials.cpp",
            "native_port/src/eor_model_surfaces.cpp",
            "native_port/src/eor_model_material_refs.cpp",
            "native_port/src/eor_native_mesh_geometry.cpp",
        }.issubset(by_name["modelAssetsArgs"]))


if __name__ == "__main__":
    unittest.main()
