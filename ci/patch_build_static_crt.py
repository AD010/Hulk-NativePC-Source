#!/usr/bin/env python3
"""Patch extracted Hulk's actual CMake invocations to select a consistent /MT CRT.

Do not set CFLAGS/CXXFLAGS globally to -fms-runtime-lib=static: Clang/CMake
otherwise emits -D_DLL and dynamic-CRT imports while linking against static
CRT libraries, causing unresolved _W_Getdays/_W_Getmonths and stdio symbols.
"""
from pathlib import Path
import sys

REPLACEMENTS = (
    (
        "Invoke-Checked 'cmake' $SdkConfigArgs $Sdk",
        "$SdkConfigArgs += '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded'\n  Invoke-Checked 'cmake' $SdkConfigArgs $Sdk",
    ),
    (
        "'-DCMAKE_CXX_COMPILER=clang++', '-DCMAKE_BUILD_TYPE=Release') $Source",
        "'-DCMAKE_CXX_COMPILER=clang++', '-DCMAKE_BUILD_TYPE=Release', "
        "'-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded') $Source",
    ),
    (
        "$cmakeArgs = @('--preset', 'win-amd64-release', '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF')",
        "$cmakeArgs = @('--preset', 'win-amd64-release', "
        "'-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', "
        "'-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')",
    ),
)

def main():
    if len(sys.argv)!=2:
        print("Usage: python ci/patch_build_static_crt.py source/BUILD_WINDOWS.ps1",file=sys.stderr)
        return 2
    path=Path(sys.argv[1])
    text=path.read_text(encoding="utf-8-sig")
    for before,after in REPLACEMENTS:
        if text.count(before)!=1:
            print(f"Refusing to patch mismatched CMake command: {before}",file=sys.stderr)
            return 1
        text=text.replace(before,after)
    path.write_text(text,encoding="utf-8",newline="\n")
    print("Applied consistent MSVC MultiThreaded /MT CRT to SDK, tests and game.")
    return 0

if __name__=="__main__":raise SystemExit(main())
