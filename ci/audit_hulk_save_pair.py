#!/usr/bin/env python3
"""Read-only Hulk save-pair integrity and schema comparison.

Compare an owner's Xbox 360 USA recomp HULK.SAV with the 2008 Windows
HULK.sav. The files are never modified or embedded in the public repo.
Both observed files use CRC32(bytes[12:]); Xbox stores CRC big-endian,
Windows stores it little-endian. Matching record keys DO NOT imply the
values or the serialized payloads are interchangeable.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zlib


def validate(data: bytes, endianness: str) -> dict:
    if endianness not in ("big", "little") or len(data) < 64:
        raise ValueError("Invalid input format or incomplete save")
    stored = int.from_bytes(data[:4], endianness)
    actual = zlib.crc32(data[12:]) & 0xFFFFFFFF
    if stored != actual:
        raise ValueError(f"Invalid {endianness}-endian save CRC32")
    return {
        "size_bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "crc32": f"{actual:08x}",
        "crc32_verified": True,
        "length_field_raw": int.from_bytes(data[4:8], endianness),
    }


def compare(xbox: bytes, pc: bytes) -> dict:
    xbox_report = validate(xbox, "big")
    pc_report = validate(pc, "little")
    # The verified pair starts its sorted 8-byte hashed-key table at
    # different offsets. More elaborate versioned tables may need a
    # disassembler/real serializer: compare only the shared prefix.
    xbox_start, pc_start = 20, 16
    limit = min((len(xbox)-xbox_start)//8, (len(pc)-pc_start)//8)
    keys_shared, values_shared = 0, 0
    for i in range(limit):
        x = xbox_start+8*i
        p = pc_start+8*i
        if int.from_bytes(xbox[x:x+4], "big") != int.from_bytes(
                pc[p:p+4], "little"):
            break
        keys_shared += 1
        if int.from_bytes(xbox[x+4:x+8], "big") == int.from_bytes(
                pc[p+4:p+8], "little"):
            values_shared += 1
    return {
        "xbox_recomp": xbox_report,
        "windows_2008": pc_report,
        "shared_leading_hashed_field_ids": keys_shared,
        "identical_values_among_shared_ids": values_shared,
        "xbox_hashed_table_start": xbox_start,
        "windows_hashed_table_start": pc_start,
        "conversion_ready": False,
        "interpretation": (
            "Shared field IDs and CRC32 prove partial schema commonality, "
            "NOT payload compatibility: Xbox is BE and the 2008 Windows "
            "game is LE with different record layouts and serialized data."
        ),
    }


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("xbox_save", type=Path)
    p.add_argument("windows_save", type=Path)
    p.add_argument("--output", type=Path)
    a = p.parse_args()
    report = compare(a.xbox_save.read_bytes(),a.windows_save.read_bytes())
    output = json.dumps(report, indent=2)+"\n"
    if a.output:
        a.output.write_text(output,encoding="utf-8")
    print(output)


if __name__=="__main__":
    main()
