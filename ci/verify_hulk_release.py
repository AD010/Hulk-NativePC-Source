#!/usr/bin/env python3
"""Check the Windows game ZIP contains exactly the EXE, one TOML, and runtime DLLs."""
import argparse
import shutil
import tempfile
import zipfile
from pathlib import Path

from audit_hulk_runtime_imports import audit_directory

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


def report_runtime_imports_in_zip(path: Path) -> None:
    """Read imported DLL names only; never place source/game bytes in logs.

    The Windows Actions job has MSVC dumpbin on PATH. This is a nonblocking
    deployment diagnostic: runtime imports do not verify Vulkan render logic,
    and a missing DLL is NOT fixed by injecting a random wrapper.
    """
    if not shutil.which("dumpbin"):
        print("[HULK/PE-IMPORTS] dumpbin unavailable; import audit skipped")
        return
    with tempfile.TemporaryDirectory(prefix="hulk-pe-imports-") as tmp:
        root = Path(tmp)
        with zipfile.ZipFile(path) as archive:
            for name in archive.namelist():
                if Path(name).suffix.lower() in (".exe", ".dll"):
                    (root / name).write_bytes(archive.read(name))
        try:
            report = audit_directory(root)
        except (ValueError, RuntimeError, OSError) as exc:
            # Never break a previously valid package due solely to a
            # missing diagnostic tool; ZIP validation above remains strict.
            print(f"[HULK/PE-IMPORTS] audit inconclusive: {exc}")
            return
    print("[HULK/PE-IMPORTS] VC++ Redist dependencies:",
          ", ".join(report["requires_system_vc_redist"]) or "none")
    print("[HULK/PE-IMPORTS] Non-system DLLs not bundled:",
          ", ".join(report["unresolved_external"]) or "none")
    # No DLL is copied automatically. Investigate each import and its
    # redistribution license before modifying the portable release.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    verified_names = verify(args.archive)
    print("Hulk release ZIP verified:", ", ".join(verified_names))
    report_runtime_imports_in_zip(args.archive)


if __name__ == "__main__":
    main()
