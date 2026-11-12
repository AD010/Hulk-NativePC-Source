#!/usr/bin/env python3
"""Lossless import of an old Hulk recomp XContent save to Windows NativePC.

DRY RUN by default. This keeps the Xbox-formatted payload unchanged and does
NOT convert it into the retail Windows-2008 HULK.sav format. Never deletes,
renames or overwrites original saves, achievements, TU1, or retail PC files.
Requires an explicit --apply and refuses an occupied target folder.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import uuid
import zlib

TITLE = "534507F1"
CONTENT_TYPE = "00000001"
SLOT = "HULK.SAV"
XUID_RE = re.compile(r"^[0-9A-Fa-f]{16}$")


def hash_file(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def ensure_nonlink(path: Path) -> None:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"Not an ordinary, non-symlink file: {path}")


def find_source(legacy_root: Path) -> tuple[Path, Path]:
    candidates = []
    for user in legacy_root.iterdir():
        if not XUID_RE.fullmatch(user.name) or not user.is_dir() or user.is_symlink():
            continue
        base = user / TITLE
        payload = base / CONTENT_TYPE / SLOT / SLOT
        header = base / "Headers" / CONTENT_TYPE / (SLOT + ".header")
        if payload.is_file() or header.is_file():
            if not all(p.is_dir() and not p.is_symlink() for p in
                       (base, base / CONTENT_TYPE, base / CONTENT_TYPE / SLOT,
                        base / "Headers", base / "Headers" / CONTENT_TYPE)):
                raise ValueError("Unexpected symlink or missing save parent directory")
            ensure_nonlink(payload)
            ensure_nonlink(header)
            candidates.append((payload, header))
    if len(candidates) != 1:
        raise ValueError(
            f"Expected exactly one legacy Hulk save package, found {len(candidates)}")
    return candidates[0]


def verify_legacy(payload: bytes, header: bytes) -> None:
    if not 64 <= len(payload) <= 16 * 1024 * 1024:
        raise ValueError("Unexpected Xbox saved game payload length")
    if int.from_bytes(payload[:4], "big") != zlib.crc32(payload[12:]) & 0xffffffff:
        raise ValueError("XContent payload CRC32 mismatch: not safe to import")
    if len(header) != 328 or header[:8] != b"\x00\x00\x00\x01" * 2:
        raise ValueError("Unrecognized Hulk XContent aggregate header")


def plan(legacy_root: Path, documents: Path) -> dict:
    payload_file, header_file = find_source(legacy_root)
    payload, header = payload_file.read_bytes(), header_file.read_bytes()
    verify_legacy(payload, header)
    destination = documents / "SEGA" / "The Incredible Hulk" / "NativePC" / "SavedGames"
    return {
        "source_payload": str(payload_file),
        "source_header": str(header_file),
        "destination_root": str(destination),
        "payload_length": len(payload),
        "payload_sha256": hash_file(payload),
        "header_length": len(header),
        "header_sha256": hash_file(header),
        "crc32_verified": True,
        "target_exists": destination.exists() or destination.is_symlink(),
        "retail_save_path_untouched": str(documents / "SEGA" / "The Incredible Hulk" / "HULK.sav"),
        "_bytes": (payload, header),
    }


def import_once(report: dict) -> dict:
    dest = Path(report["destination_root"])
    if dest.exists() or dest.is_symlink():
        raise FileExistsError(f"Existing native save directory; refuse overwrite: {dest}")
    parent = dest.parent
    parent.mkdir(parents=True, exist_ok=True)
    stage = parent / (".hulk-import-" + uuid.uuid4().hex)
    stage.mkdir(mode=0o700)
    payload, header = report["_bytes"]
    try:
        (stage / SLOT).mkdir()
        (stage / "Headers").mkdir()
        for path, data in ((stage / SLOT / SLOT, payload),
                           (stage / "Headers" / (SLOT + ".header"), header)):
            with path.open("xb") as output:
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
        if dest.exists() or dest.is_symlink():
            raise FileExistsError("Destination became occupied during staging")
        os.rename(stage, dest)
    except Exception:
        # Preserve staged data for recovery; NEVER modify/delete originals.
        raise
    expected = ((dest / SLOT / SLOT, report["payload_sha256"]),
                (dest / "Headers" / (SLOT + ".header"), report["header_sha256"]))
    if any(hash_file(path.read_bytes()) != digest for path, digest in expected):
        raise RuntimeError("Post-import integrity verification failed")
    return {
        "status": "imported", "destination": str(dest),
        "verified_payload_sha256": report["payload_sha256"],
        "verified_header_sha256": report["header_sha256"],
    }


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("legacy_root", type=Path)
    p.add_argument("documents", type=Path)
    p.add_argument("--apply", action="store_true")
    args = p.parse_args()
    report = plan(args.legacy_root, args.documents)
    if args.apply:
        report.update(import_once(report))
    else:
        report["status"] = "dry_run_only"
    report.pop("_bytes", None)
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
