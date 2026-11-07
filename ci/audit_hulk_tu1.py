#!/usr/bin/env python3
"""Verify the user's Xbox 360 Hulk TU1 and safely extract it to a separate folder.

Does not overwrite disc ARC/IND files, patch a XEX, or install a game update.
The ReXGlue XEX loader applies a sibling default.xexp during codegen, but the
updated guest function map and all graphics hooks must be validated first.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import zipfile
from pathlib import Path

ZIP_SHA256 = "3792037f8af804d34f4c264188597222b0d8ffe982ad64ba8136f3fcde767b50"
STFS_FILENAME = "TU_19KA1VH_0000008000000.0000000000082"
TITLE_ID, MEDIA_ID = 0x534507F1, 0x2BA1D2F6
BLOCK_SIZE, HASH_INTERVAL = 4096, 170


def u32(data: bytes, at: int) -> int:
    return struct.unpack_from(">I", data, at)[0]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def data_offset(block: int) -> int:
    group = block // HASH_INTERVAL
    return 0xC000 + BLOCK_SIZE * (block + group + (group > 0))


def hash_offset(block: int) -> int:
    group = block // HASH_INTERVAL
    return data_offset(group * HASH_INTERVAL) - BLOCK_SIZE + (block % HASH_INTERVAL) * 24


def xex_rsa_digest(data: bytes) -> bytes:
    if len(data) < 0x118 or data[:4] != b"XEX2":
        raise ValueError("Not an Xbox 360 XEX2 executable")
    sec = u32(data, 0x10)
    if sec < 0x18 or sec + 0x108 > len(data):
        raise ValueError("Invalid XEX2 security offset")
    return hashlib.sha1(data[sec + 8: sec + 0x108]).digest()


def xexp_source(data: bytes) -> tuple[bytes, int, int]:
    if len(data) < 0x30 or data[:4] != b"XEX2":
        raise ValueError("Invalid XEXP/XEX2 magic")
    count = u32(data, 0x14)
    if count > 1024 or 0x18 + count * 8 > len(data):
        raise ValueError("Invalid XEXP optional header table")
    for index in range(count):
        key_at = 0x18 + 8 * index
        if u32(data, key_at) == 0x000005FF:
            at = u32(data, key_at + 4)
            if at + 0x4C > len(data):
                raise ValueError("Truncated XEXP delta descriptor")
            return data[at + 0xC: at + 0x20], u32(data, at + 8), u32(data, at + 4)
    raise ValueError("XEXP delta patch descriptor not found")


def files_in_stfs(data: bytes) -> dict[str, bytes]:
    if len(data) < 0xC000 or data[:4] != b"LIVE":
        raise ValueError("Unexpected STFS package type")
    if (u32(data, 0x344), u32(data, 0x354), u32(data, 0x360)) != (
        0x000B0000, MEDIA_ID, TITLE_ID
    ):
        raise ValueError("Title Update content type, media ID or title ID mismatch")
    if data[0x971A:0x971E] != b"TUPD":
        raise ValueError("Expected TUPD signature not found")

    table_at = data_offset(0)
    table = data[table_at:table_at + BLOCK_SIZE]
    if hashlib.sha1(table).digest() != data[hash_offset(0):hash_offset(0) + 20]:
        raise ValueError("STFS file table SHA1 mismatch")

    entries = []
    for index in range(BLOCK_SIZE // 64):
        row = table[64 * index:64 * (index + 1)]
        if not row[0]:
            break
        length = row[0x28] & 0x3F
        if not 1 <= length <= 40:
            raise ValueError("Invalid STFS entry name length")
        name = row[:length].decode("ascii")
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", name) or name in (".", ".."):
            raise ValueError("Unsafe STFS filename")
        entries.append({
            "name": name, "directory": bool(row[0x28] & 0x80),
            "blocks": int.from_bytes(row[0x29:0x2C], "little"),
            "first": int.from_bytes(row[0x2F:0x32], "little"),
            "parent": int.from_bytes(row[0x32:0x34], "big"),
            "size": u32(row, 0x34),
        })

    result = {}
    for index, entry in enumerate(entries):
        chain = [entry["name"]]
        parent = entry["parent"]
        visited = {index}
        while parent != 0xFFFF:
            if parent >= len(entries) or parent in visited or not entries[parent]["directory"]:
                raise ValueError("Invalid STFS parent chain")
            visited.add(parent)
            chain.append(entries[parent]["name"])
            parent = entries[parent]["parent"]
        path = "/".join(reversed(chain))
        if entry["directory"]:
            if entry["blocks"] or entry["size"]:
                raise ValueError("Directory has allocated data")
            continue
        if path.casefold() != "default.xexp" and not path.lower().endswith((".arc", ".ind")):
            raise ValueError("Unexpected TU file " + path)
        if entry["size"] > entry["blocks"] * BLOCK_SIZE:
            raise ValueError("STFS file size exceeds allocated blocks")
        contents = []
        block = entry["first"]
        seen = set()
        for step in range(entry["blocks"]):
            if block in seen:
                raise ValueError("Circular STFS data chain")
            seen.add(block)
            at, ht = data_offset(block), hash_offset(block)
            if at + BLOCK_SIZE > len(data) or ht + 24 > len(data):
                raise ValueError("STFS block out of bounds")
            payload = data[at:at + BLOCK_SIZE]
            if hashlib.sha1(payload).digest() != data[ht:ht + 20]:
                raise ValueError(f"STFS block SHA1 mismatch for {path}, block {step}")
            contents.append(payload)
            successor = int.from_bytes(data[ht + 21:ht + 24], "big")
            if step + 1 == entry["blocks"]:
                if successor != 0xFFFFFF:
                    raise ValueError("STFS chain missing EOF marker")
            elif successor == 0xFFFFFF:
                raise ValueError("Premature STFS EOF")
            block = successor
        if path.casefold() in {name.casefold() for name in result}:
            raise ValueError("Duplicate STFS path")
        result[path] = b"".join(contents)[:entry["size"]]
    return result


def audit(archive: Path, base_xex: Path | None = None, output: Path | None = None):
    raw = archive.read_bytes()
    if digest(raw) != ZIP_SHA256:
        raise ValueError("TU1 ZIP SHA256 mismatch: unexpected archive")
    with zipfile.ZipFile(archive) as z:
        infos = z.infolist()
        if len(infos) != 1 or infos[0].filename != STFS_FILENAME:
            raise ValueError("Unexpected entries in TU1 ZIP")
        if infos[0].file_size > 100 * 1024 * 1024:
            raise ValueError("Unreasonable STFS uncompressed size")
        stfs = z.read(infos[0])
    files = files_in_stfs(stfs)
    if len(files) != 31 or "default.xexp" not in files:
        raise ValueError("Unexpected TU1 file inventory")
    xexp_hash, source_version, target_version = xexp_source(files["default.xexp"])
    if (source_version, target_version) != (2, 0x00000102):
        raise ValueError("Unexpected XEXP source and target version")
    match = None
    if base_xex is not None:
        match = xex_rsa_digest(base_xex.read_bytes()) == xexp_hash
        if not match:
            raise ValueError("XEXP base signature does not match this default.xex")
    report = {
        "title_id": f"{TITLE_ID:08X}", "media_id": f"{MEDIA_ID:08X}",
        "source_version": f"{source_version:08X}",
        "target_version": f"{target_version:08X}",
        "xexp_source_rsa_sha1": xexp_hash.hex(),
        "xex_signature_match": match,
        "stfs_sha256": digest(stfs),
        "files": [
            {"path": name, "size": len(blob), "sha256": digest(blob)}
            for name, blob in sorted(files.items())
        ],
    }
    # Finish validation before creating an extraction directory.
    if output is not None:
        if output.exists():
            raise ValueError("Refusing to overwrite an existing output directory")
        output.mkdir(parents=True)
        for name, blob in files.items():
            path = output / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(blob)
        (output / "verified-tu1.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8"
        )
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("archive", type=Path)
    ap.add_argument("--base-xex", type=Path, help="Verify original XEX RSA signature")
    ap.add_argument("--output", type=Path, help="Extract to NEW staging directory only")
    ap.add_argument("--report", type=Path, help="Write verification report JSON")
    args = ap.parse_args()
    result = audit(args.archive, args.base_xex, args.output)
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Hulk TU1 verified: {len(result['files'])} files, STFS block hashes checked")
    print("XEXP is NOT applied. Original disc ARC/IND files have NOT been modified.")
    if result["xex_signature_match"] is not None:
        print("Base XEX signature: MATCH")


if __name__ == "__main__":
    main()
