"""Source-level regression contracts for the original Win32 D3D9 ASI.

Run: python -m unittest discover -s pc2008_mod/tests -p 'test_d3d9_hook_contract.py'
These checks do not substitute for an x86 build or GPU acceptance.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "HulkPC.cpp").read_text(encoding="utf-8")

# IDirect3D9::CreateDevice and IDirect3DDevice9 indices in d3d9.h.
EXPECTED = {
    "OnCreateDevice": 16,
    "OnReset": 16,
    "OnPresent": 17,
    "OnSampler": 69,
    "OnCreateVS": 91,
    "OnSetVS": 92,
    "OnVSConstants": 94,
    "OnCreatePS": 106,
}


class D3D9HookContracts(unittest.TestCase):
    def test_expected_vtable_slots(self):
        for hook, index in EXPECTED.items():
            with self.subTest(hook=hook):
                pattern = rf"ReplaceVtable\([^;]*?,\s*{index}\s*,\s*&{hook}\s*,"
                self.assertRegex(SOURCE, pattern)

    def test_original_game_gate_before_iat_patch(self):
        body = SOURCE.split("bool HookIat() {", 1)[1].split("\nDWORD WINAPI TryLateHook", 1)[0]
        self.assertLess(body.index("if(!OriginalHulk())return false;"),
                        body.index("VirtualProtect(thunk"))

    def test_failed_reset_does_not_invalidate_shader(self):
        body = SOURCE.rsplit("HRESULT STDMETHODCALLTYPE OnReset(IDirect3DDevice9* dev,", 1)[-1]
        body = body.split("\nHRESULT STDMETHODCALLTYPE OnPresent(", 1)[0]
        self.assertIn("const HRESULT firstHr=realReset(dev,pp);", body)
        self.assertIn("HRESULT hr=firstHr;", body)
        self.assertRegex(body, r"if\(SUCCEEDED\(hr\)\)\s*\{[^}]*activeVS=nullptr;")

    def test_reset_fullscreen_override_and_fallback(self):
        body = SOURCE.rsplit("HRESULT STDMETHODCALLTYPE OnReset(IDirect3DDevice9* dev,", 1)[-1]
        body = body.split("\nHRESULT STDMETHODCALLTYPE OnPresent(", 1)[0]
        self.assertIn("pp && width && height && !pp->Windowed", body)
        self.assertIn("original=*pp;", body)
        self.assertIn("pp->BackBufferWidth=width;", body)
        self.assertIn("pp->BackBufferHeight=height;", body)
        self.assertRegex(body, r"if\\(FAILED\\(hr\\) && changed\\)\\s*\\{\\s*\\*pp=original;\\s*hr=realReset\\(dev,pp\\);")
        self.assertLess(body.index("hr=realReset(dev,pp);"),
                        body.index("if(SUCCEEDED(hr))"))
        self.assertIn("activeVS=nullptr;", body)

    def test_hud_protection_defaults_safe(self):
        self.assertIn('L"ProtectOriginalHUD",1,ini', SOURCE)
        self.assertRegex(SOURCE, r"fixAspect=false;\s*width=height=0;")

    def test_no_d3d9_or_dxvk_file_replacement(self):
        # The ASI hooks the existing d3d9 module, including DXVK when present.
        self.assertIn('GetModuleHandleW(L"d3d9.dll")', SOURCE)
        self.assertNotIn('LoadLibraryW(L"d3d9.dll")', SOURCE)
        self.assertNotRegex(SOURCE, r'(?:CopyFile|MoveFile|DeleteFile)[AW]?\s*\(')

    def test_experimental_profiles_explicitly_opt_out(self):
        for name in ("HulkPC_2560x1600_HorPlus.ini",
                     "HulkPC_2560x1600_NoFov.ini"):
            with self.subTest(profile=name):
                data = (ROOT / "profiles" / name).read_text(encoding="utf-8-sig")
                self.assertRegex(data, r"(?m)^ProtectOriginalHUD\s*=\s*0\s*(?:[;#].*)?$")


if __name__ == "__main__":
    unittest.main()
