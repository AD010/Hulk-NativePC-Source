#!/usr/bin/env python3
"""Check the Windows game ZIP contains exactly the EXE, one TOML, and runtime DLLs."""
import argparse
import zipfile
from pathlib import Path

CORE = {"incredible_hulk.exe", "incredible_hulk.toml", "rexruntime.dll", "rexgpu-xenos.dll"}
OPTIONAL = {"amd_fidelityfx_vk.dll", "nvngx_dlss.dll"}


def verify(path: Path):
    with zipfile.ZipFile(path) as z:
        entries = z.infolist()
        names = [info.filename for info in entries]
        if len({name.casefold() for name in names}) != len(names):
            raise ValueError("Duplicate archive entries (case-insensitive)")
        if any(info.is_dir() or "/" in info.filename or "\\" in info.filename for info in entries):
            raise ValueError("Unexpected subfolder in release ZIP")
        missing = CORE - set(names)
        extra = set(names) - CORE - OPTIONAL
        if missing or extra:
            raise ValueError(f"Missing: {sorted(missing)}; unexpected: {sorted(extra)}")
        if any(info.file_size == 0 for info in entries):
            raise ValueError("Release contains empty files")
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    print("Hulk release ZIP verified:", ", ".join(verify(args.archive)))


if __name__ == "__main__":
    main()
