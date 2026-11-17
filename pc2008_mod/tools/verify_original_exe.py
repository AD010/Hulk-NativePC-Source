"""Fail-closed preflight for the user's original 2008 Win32 HULK.EXE.

Usage: python pc2008_mod/tools/verify_original_exe.py "C:\\Games\\Hulk\\HULK.EXE"
This tool never modifies the game, installs hooks, or uploads executable data.
"""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import sys

EXPECTED_SHA256 = "6d71e479de73a5e3ee5f3a924ed2dc6f79aea9f0aac43b5984b1f77193d3b233"
EXPECTED_TIMESTAMP = 0x48503EC6
EXPECTED_ENTRY = 0x24E129
EXPECTED_IMAGE_SIZE = 0x81B000


def verify(path: Path) -> tuple[bool, list[str]]:
    errors = []
    try:
        # Hash and parse the same open file, not two separately resolved paths.
        with path.open("rb") as stream:
            before = os.fstat(stream.fileno())
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
            if digest.lower() != EXPECTED_SHA256:
                errors.append("SHA256 mismatch: unsupported HULK.EXE; no patching allowed")
            stream.seek(0)
            dos = stream.read(64)
            if len(dos) != 64 or dos[:2] != b"MZ":
                return False, errors + ["Missing DOS MZ header"]
            peoff = struct.unpack_from("<I", dos, 0x3C)[0]
            size = before.st_size
            if peoff != 0x40 or peoff + 0x78 > size:
                return False, errors + ["Unexpected or truncated PE header offset"]
            stream.seek(peoff)
            pe = stream.read(0x78)
            after = os.fstat(stream.fileno())
            if (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
                after.st_size, after.st_mtime_ns, after.st_ctime_ns
            ):
                return False, errors + ["Executable changed during verification"]
        if pe[:4] != b"PE" + bytes(2):
            errors.append("Missing PE signature")
        machine, sections, timestamp = struct.unpack_from("<HHI", pe, 4)
        optional_size = struct.unpack_from("<H", pe, 20)[0]
        magic = struct.unpack_from("<H", pe, 24)[0]
        entry = struct.unpack_from("<I", pe, 40)[0]
        image_size = struct.unpack_from("<I", pe, 80)[0]
        if machine != 0x14C or magic != 0x10B or optional_size < 0x60 or not sections:
            errors.append("Not the expected PE32/i386 executable")
        # The section table must fit entirely inside the file. A plausible
        # COFF header alone is not sufficient evidence of a valid PE image.
        section_table_end = peoff + 24 + optional_size + sections * 40
        if section_table_end > size or section_table_end < peoff:
            errors.append("Truncated PE optional header or section table")
        if timestamp != EXPECTED_TIMESTAMP or entry != EXPECTED_ENTRY or image_size != EXPECTED_IMAGE_SIZE:
            errors.append("PE identity mismatch (timestamp/entry point/image size)")
    except (OSError, ValueError, struct.error) as exc:
        return False, errors + [f"Cannot inspect executable: {exc}"]
    return not errors, errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path, help="Path to original HULK.EXE")
    args = parser.parse_args()
    ok, errors = verify(args.exe)
    if ok:
        print("PASS: exact original PC HULK.EXE SHA256 and PE32/i386 identity")
        return 0
    for error in errors:
        print("FAIL:", error, file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
