#!/usr/bin/env python3
"""Deterministic synthetic SDK audit without Xbox binaries or system DLLs."""
import tempfile
import unittest
from pathlib import Path
from audit_hulk_sdk_services import audit_sdk, FEATURES

class TestSdkServices(unittest.TestCase):
    def write_fake_sdk(self, root):
        files={}
        for markers in FEATURES.values():
            for name,marker in markers:
                files.setdefault(name,[]).append(marker)
        file="src/kernel/xam/xam_content.cpp"
        files[file]+= [
            "REX_EXPORT(__imp__XamContentCreate, rex::kernel::xam::XamContentCreate_entry)",
            "REX_EXPORT(__imp__XamContentResolve, rex::kernel::xam::XamContentResolve_entry)",
            'REXKRNL_WARN("XamContentResolve unimplemented!");',
            "REX_EXPORT_STUB(__imp__XamContentMountPackage);",
        ]
        for name,parts in files.items():
            path=root/name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text("\n".join(parts),encoding="utf-8")
    def test_base_sdk_service_evidence_and_stub_classification(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk=Path(directory)
            self.write_fake_sdk(sdk)
            report=audit_sdk(sdk)
            self.assertEqual(len(report["services"]),6)
            self.assertTrue(all(r["source_implementation_present"]
                                for r in report["services"].values()))
            self.assertIn("XamContentMountPackage",
                          report["xam_content"]["explicit_stub_exports"])
            self.assertIn("XamContentResolve",
                          report["xam_content"]["exported_but_body_incomplete"])
            self.assertFalse(report["conclusions"]["native_audio_playback_verified"])
            self.assertFalse(report["conclusions"]["physics_recompiled_and_60fps_verified"])
            self.assertFalse(report["xam_content"]["game_reachability_proved"])
            self.assertFalse(any(r["verified_in_hulk_gameplay"]
                                 for r in report["services"].values()))
    def test_missing_audio_degrades_without_fabricated_success(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk=Path(directory)
            self.write_fake_sdk(sdk)
            (sdk/"src/audio/xma_context.cpp").unlink()
            result=audit_sdk(sdk)
            self.assertFalse(result["services"]["audio_xma_decoder"]
                             ["source_implementation_present"])
            self.assertIsNone(result["conclusions"]["xma_reimplementation_required"])
            self.assertTrue(result["services"]["kernel_host_threads"]
                            ["source_implementation_present"])
    def test_duplicate_export_is_not_a_proven_implementation(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk=Path(directory)
            self.write_fake_sdk(sdk)
            p=sdk/"src/kernel/xam/xam_content.cpp"
            p.write_text(p.read_text()+"\nREX_EXPORT_STUB(__imp__XamContentCreate);")
            with self.assertRaises(ValueError):
                audit_sdk(sdk)
if __name__=="__main__":
    unittest.main()
