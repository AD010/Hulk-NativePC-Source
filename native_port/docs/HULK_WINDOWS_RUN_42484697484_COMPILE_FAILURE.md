# Build failure diagnosis — 8 November 2026

Private file: Hulk-PrivateRunner-Diagnostics.zip (received from user).
Source: hulk-compiler-private.log from GitHub Actions run #42484697484,
commit e04cb056bd01. Last 1159-object C++ link pass stopped at
[1148/1159] compiling native_port/src/launcher/game_window.cpp.

Exact clang++ diagnostic at source line 62:
- LoadCursorW(nullptr, IDC_ARROW): `no matching function`.
- IDC_ARROW in non-UNICODE Windows SDK build is LPSTR char*,
  but explicit LoadCursorW expects LPCWSTR const wchar_t*.
- Other identical native code uses LoadCursorA(nullptr, IDC_ARROW);
  change this call to LoadCursorA, preserving the project's ANSI
  cursor convention, with no effect on guest Vulkan drawing.

No other C++ fatal error appears in this private compile log. The
CMake probe lines (pthreads, iconv etc) and NGX warnings are NOT this
failure; the single fatal C++ error was LoadCursorW mismatch. 1153 of
1159 tasks progressed before Ninja stopped; compiler diagnostics
explicitly identify this as the blocking issue.

ci/test_hulk_win32_cursor_compile.py now enforces source invariants
and, on real Windows runner, invokes clang++ -fsyntax-only over the
WinAPI LoadCursorA fixture *before* costly SDK and guest C++ compilation.
The full build must still succeed to confirm this repair.
No copyrighted XEX/XEXP, native scene textures or shaders uploaded.
