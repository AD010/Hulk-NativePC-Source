#!/usr/bin/env python3
"""Hulk 2008 EoR: extract exact named resources via verified index hash.

Original assets are user-owned and PRIVATE. This tool must not put their
payloads or decrypted XEX into the public source-only GitHub repository.

Example, from an external private working directory:
  python ci/hulk_eor_extract_named_assets.py \
    --index indexPC.ind --archive 'Fonts=FONTS(1).ARC' \
    --name 'Fonts:font_clean' --out-dir D:/PrivateHulk/ui_pc

Xbox, same resource ID but different payload:
  python ci/hulk_eor_extract_named_assets.py \
    --index indexXBOX360.ind --archive 'Fonts=FONTS.ARC' \
    --name 'Fonts:font_clean' --out-dir D:/PrivateHulk/ui_xbox

Resources are *raw*, not automatically decoded as DDS/PNG/FDF.
QuickBMS: incredible_hulk_2008.bms by aluigi is an alternative ARC
extractor (known from the 2019 Hulk index.ind thread).
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import zlib

from audit_hulk_pc_x360_ui_assets import parse_index

REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
MAX_RESOURCE = 256 << 20
CATEGORIES = re.compile(r"^[A-Za-z][A-Za-z0-9]{0,48}$")
SAFE_NAME = re.compile(r"^[A-Za-z0-9_.-]{1,128}$")


def resource_hash(name: str) -> str:
    """CRC32(ASCII uppercase name), verified against three EoR dataset IDs."""
    if not SAFE_NAME.fullmatch(name):
        raise ValueError("EoR asset name must be ASCII and have no path separators")
    normalized = name.upper()
    # EoR serialized _app_ uses type-tagged links (TXFL..., LFXT...),
    # while both ARC indexes hash the base ui_* name, not the 4-char
    # type tag. This was proven against five original PC/Xbox index
    # records: menuitems, menubands, WASD, Xbox A and left mouse button.
    if normalized.startswith(("TXFLUI_", "LFXTUI_")):
        normalized = normalized[4:]
    return f"{zlib.crc32(normalized.encode('ascii')) & 0xFFFFFFFF:08X}"


def labeled(value: str) -> tuple[str, str]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("Expected Category=archive-path")
    category, path = value.split("=", 1)
    if not CATEGORIES.fullmatch(category) or not path:
        raise argparse.ArgumentTypeError("Invalid Category=archive-path")
    return category, path


def selector(value: str) -> tuple[str, str]:
    if ":" not in value:
        raise argparse.ArgumentTypeError("Expected Category:resource_name")
    category, name = value.split(":", 1)
    if not CATEGORIES.fullmatch(category) or not SAFE_NAME.fullmatch(name):
        raise argparse.ArgumentTypeError("Invalid resource selector")
    return category, name


def extract(index_file: Path, archives: dict[str, Path],
            requests: list[tuple[str, str]], out_dir: Path,
            max_resource: int = MAX_RESOURCE) -> dict:
    # Explicit private destination; no accidental publication to Git.
    out = out_dir.resolve()
    if out == REPOSITORY_ROOT or REPOSITORY_ROOT in out.parents:
        raise ValueError("Never extract copyrighted game assets inside public source repository")
    if max_resource <= 0 or max_resource > (1 << 30):
        raise ValueError("Unreasonable per-resource cap")
    index = parse_index(index_file.read_bytes())
    groups = index["groups"]
    result = {
        "endianness": index["endianness"],
        "index_sha256": hashlib.sha256(index_file.read_bytes()).hexdigest(),
        "records": [], "asset_bytes_embedded": False,
    }
    # Validate the entire request list before creating output files.
    entries = []
    for category, name in requests:
        if category not in groups:
            raise ValueError(f"Archive category not present in index: {category}")
        if category not in archives:
            raise ValueError(f"Missing private .ARC for {category}")
        digest = resource_hash(name)
        row = next((row for row in groups[category]
                    if row["hash"] == digest), None)
        if row is None:
            raise LookupError(f"No {category} index entry for {name!r} ({digest})")
        if row["size"] > max_resource or row["size"] <= 0:
            raise ValueError(f"Unsupported resource size for {category}:{name}")
        archive = archives[category].resolve()
        if not archive.is_file():
            raise FileNotFoundError(archive)
        if row["offset"] < 4 or row["offset"] + row["size"] > archive.stat().st_size:
            raise ValueError(f"Out-of-bounds resource in {archive.name}")
        entries.append((category, name, digest, row, archive))
    for category, name, digest, row, archive in entries:
        # Streaming copy; one isolated (hash-named) file for each resource.
        destination = out / category / (digest + ".bin")
        destination.parent.mkdir(parents=True, exist_ok=True)
        h = hashlib.sha256()
        with archive.open("rb") as src, destination.open("wb") as dest:
            src.seek(row["offset"])
            remain = row["size"]
            while remain:
                block = src.read(min(remain, 1 << 20))
                if not block:
                    raise IOError("Truncated resource despite earlier bounds check")
                dest.write(block)
                h.update(block)
                remain -= len(block)
        result["records"].append({
            "category": category, "source_name": name,
            "hash": digest, "size": row["size"],
            "archive": archive.name, "archive_offset": row["offset"],
            "sha256": h.hexdigest(),
            "relative_private_file": f"{category}/{digest}.bin",
            "payload_format_verified": False,
        })
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--index", required=True, type=Path)
    ap.add_argument("--archive", action="append", default=[], type=labeled,
                    help="Category=path to original archive; repeat per category")
    ap.add_argument("--name", action="append", default=[], type=selector,
                    help="Category:original_asset_name; repeated")
    ap.add_argument("--out-dir", required=True, type=Path,
                    help="PRIVATE folder outside public source checkout")
    ap.add_argument("--json", type=Path, help="Optional sanitized report")
    args = ap.parse_args()
    if not args.name:
        ap.error("Provide at least one --name")
    report = extract(args.index,
                     {k: Path(v) for k, v in args.archive},
                     args.name, args.out_dir)
    text = json.dumps(report, indent=2, ensure_ascii=False)
    if args.json:
        args.json.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
