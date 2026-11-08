#!/usr/bin/env python3
"""Fast Windows compile preflight for Win32 ANSI/WIDE cursor ABI.

This caught the REAL failed build in #42484697484:
LoadCursorW(nullptr, IDC_ARROW) while UNICODE is not defined, giving
LPSTR to a function expecting LPCWSTR. Run before expensive PPC+NativeX.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILE = ROOT / "native_port/src/launcher/game_window.cpp"

class TestWin32CursorCompatibility(unittest.TestCase):
    def test_game_window_cursor_functions_match_char_type(self):
        source = FILE.read_text(encoding="utf-8")
        self.assertNotRegex(source, r"\bLoadCursorW\s*\(\s*nullptr\s*,\s*IDC_ARROW\s*\)")
        self.assertGreaterEqual(source.count("LoadCursorA(nullptr, IDC_ARROW)"), 3)
        self.assertIn("msg == WM_SYSKEYDOWN", source)
        self.assertIn("PcMenuToggle();", source)

    @unittest.skipUnless(sys.platform == "win32", "Windows SDK compiler check")
    def test_compile_real_win32_cursor_signature(self):
        fixture = r"""
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
void HulkCursorSmoke() {
  SetCursor(LoadCursorA(nullptr, IDC_ARROW));
}
"""
        compiler = os.environ.get("CXX", "clang++")
        result = subprocess.run(
            [compiler, "-std=c++20", "-fsyntax-only", "-x", "c++", "-"],
            input=fixture, text=True, capture_output=True, check=False
        )
        self.assertEqual(
            result.returncode, 0,
            "Windows WinAPI cursor ABI smoke compile failed:\n" +
            result.stdout + result.stderr
        )

if __name__ == "__main__":
    unittest.main()
